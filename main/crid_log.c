/**
 * @file crid_log.c
 * @brief 接收端飞行轨迹持久化：flash 环形日志实现
 *
 * 存储模型（分区 ridlog，4 MB）：
 *   sector 0        meta：magic + 代次 gen + crc，只占 12 字节
 *   sector 1..1023  数据环：每扇区 64 条 64 字节记录
 *
 * 写：从 (wsec, wslot) 顺序追加，槽位 0 时先擦该扇区，写满一圈回到 0 号
 *     扇区继续，天然覆盖最旧数据。写任务独立于接收解析任务，避免擦除的
 *     几十毫秒阻塞混杂模式回调。
 *
 * 恢复：上电只读每个扇区的首条记录（1024 次 64 字节读，约 0.1 s），
 *      seq 最大者即"最后写到哪个扇区"，再在该扇区内找第一个空槽。
 *
 * 掉电：擦除后即写入，掉电最多丢正在写的那一条；被擦但没写成的扇区
 *      首记录为 0xFF，扫描时视为空，写指针回到该处，不会污染时间顺序。
 */

#include "crid_log.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "CN_C-RID_LOG";

#define REC_MAGIC   0x474F4C52u     // 'RLOG'
#define META_MAGIC  0x454D4C52u     // 'RLME'
#define SECTOR_SIZE 4096
#define PER_SEC     (SECTOR_SIZE / CRID_LOG_REC_SIZE)
#define MAX_TARGETS 16
#define QUEUE_LEN   24
#define SEED_SCAN   512             // 上电重建目标表时回扫的条数

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t gen;
    uint16_t rsv;
    uint32_t crc;
} log_meta_t;

_Static_assert(sizeof(crid_log_rec_t) == CRID_LOG_REC_SIZE, "记录必须是 64 字节");

static const esp_partition_t *s_part;
static uint16_t s_secs;         // 数据扇区数（不含 meta）
static uint16_t s_gen;
static uint32_t s_seq;
static uint16_t s_wsec;         // 数据扇区逻辑编号 0..s_secs-1
static uint16_t s_wslot;        // 扇区内槽位 0..PER_SEC-1
static uint32_t s_count;
static uint32_t s_dropped;
static bool     s_ready;

static QueueHandle_t     s_q;
static SemaphoreHandle_t s_mux;
static crid_log_target_t s_tg[MAX_TARGETS];
static int               s_tgn;
// 遍历用扇区缓冲：不放栈（httpd 任务栈只有 8 KB），且要求 4 字节对齐
static uint8_t s_sbuf[SECTOR_SIZE] __attribute__((aligned(4)));

/* ---------------- 基础工具 ---------------- */

static uint16_t crc16(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        }
    }
    return c;
}

static inline uint32_t sec_off(uint16_t sec) {
    return (uint32_t)(sec + 1) * SECTOR_SIZE;
}

static inline uint32_t capacity(void) {
    return (uint32_t)s_secs * PER_SEC;
}

static bool rec_ok(const crid_log_rec_t *r) {
    return r->magic == REC_MAGIC && r->gen == s_gen &&
           r->crc == crc16(r, offsetof(crid_log_rec_t, crc));
}

static bool read_abs(uint32_t abs, crid_log_rec_t *r) {
    uint16_t sec  = (uint16_t)(abs / PER_SEC);
    uint16_t slot = (uint16_t)(abs % PER_SEC);
    if (sec >= s_secs) return false;
    // packed 结构的对齐只有 1，不能直接把调用方的指针交给 flash 读取接口
    crid_log_rec_t tmp __attribute__((aligned(4)));
    if (esp_partition_read(s_part, sec_off(sec) + (uint32_t)slot * CRID_LOG_REC_SIZE,
                           &tmp, CRID_LOG_REC_SIZE) != ESP_OK) {
        return false;
    }
    if (!rec_ok(&tmp)) return false;
    *r = tmp;
    return true;
}

// 最新一条记录的环形绝对索引（调用方需保证 s_count > 0）
static uint32_t newest_abs(void) {
    uint32_t a = (uint32_t)s_wsec * PER_SEC + s_wslot;
    if (a == 0) a = capacity();
    return a - 1;
}

/* ---------------- 目标表 ---------------- */

static void target_update(const crid_log_rec_t *r, bool count) {
    for (int i = 0; i < s_tgn; i++) {
        if (memcmp(s_tg[i].mac, r->mac, 6) != 0) continue;
        if (count) s_tg[i].count++;
        if (r->t_unix) {
            if (!s_tg[i].oldest_unix || r->t_unix < s_tg[i].oldest_unix)
                s_tg[i].oldest_unix = r->t_unix;
            if (r->t_unix > s_tg[i].newest_unix)
                s_tg[i].newest_unix = r->t_unix;
        }
        s_tg[i].newest_tick = r->tick_ms;
        s_tg[i].last_alt_m   = r->alt_m;
        s_tg[i].last_lat_e7  = r->lat_e7;
        s_tg[i].last_lon_e7  = r->lon_e7;
        if (r->uid[0]) {
            memcpy(s_tg[i].uid, r->uid, sizeof(s_tg[i].uid) - 1);
            s_tg[i].uid[sizeof(s_tg[i].uid) - 1] = '\0';
        }
        return;
    }

    int slot;
    if (s_tgn < MAX_TARGETS) {
        slot = s_tgn++;
    } else {
        // 表满：替换最久没出现的那个
        slot = 0;
        for (int i = 1; i < s_tgn; i++) {
            if (s_tg[i].newest_tick < s_tg[slot].newest_tick) slot = i;
        }
    }
    crid_log_target_t *t = &s_tg[slot];
    memset(t, 0, sizeof(*t));
    memcpy(t->mac, r->mac, 6);
    memcpy(t->uid, r->uid, sizeof(t->uid) - 1);
    t->uid[sizeof(t->uid) - 1] = '\0';
    t->count = count ? 1 : 0;
    t->oldest_unix = r->t_unix;
    t->newest_unix = r->t_unix;
    t->newest_tick = r->tick_ms;
    t->last_alt_m  = r->alt_m;
    t->last_lat_e7 = r->lat_e7;
    t->last_lon_e7 = r->lon_e7;
}

/* ---------------- 写任务 ---------------- */

// 写入一条记录：调用方负责持有 s_mux
static esp_err_t store_one(crid_log_rec_t *r) {
    r->magic = REC_MAGIC;
    r->gen   = s_gen;
    r->seq   = s_seq;
    r->crc   = crc16(r, offsetof(crid_log_rec_t, crc));

    // 一个扇区的第一条：先擦除（环形覆盖最旧数据）
    esp_err_t err = ESP_OK;
    if (s_wslot == 0) {
        err = esp_partition_erase_range(s_part, sec_off(s_wsec), SECTOR_SIZE);
    }
    if (err == ESP_OK) {
        err = esp_partition_write(s_part,
                                  sec_off(s_wsec) + (uint32_t)s_wslot * CRID_LOG_REC_SIZE,
                                  r, CRID_LOG_REC_SIZE);
    }
    if (err == ESP_OK) {
        s_seq++;
        if (s_count < capacity()) s_count++;
        target_update(r, true);
    } else {
        ESP_LOGW(TAG, "写记录失败 @%u/%u: %s", s_wsec, s_wslot, esp_err_to_name(err));
    }

    if (++s_wslot >= PER_SEC) {
        s_wslot = 0;
        s_wsec  = (uint16_t)((s_wsec + 1) % s_secs);
    }
    return err;
}

static void log_task(void *pv) {
    (void)pv;
    // static：避开 packed 结构在栈上的对齐不确定性（flash 写要求 4 字节对齐）
    static crid_log_rec_t r __attribute__((aligned(4)));
    while (1) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        if (xSemaphoreTake(s_mux, portMAX_DELAY) != pdTRUE) continue;
        store_one(&r);
        xSemaphoreGive(s_mux);
    }
}

/* ---------------- 对外接口 ---------------- */

esp_err_t crid_log_init(void) {
    if (s_ready) return ESP_OK;

    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      CRID_LOG_PART_SUBTYPE, CRID_LOG_PART_NAME);
    if (s_part == NULL) {
        ESP_LOGW(TAG, "未找到 %s 分区，轨迹记录不可用（分区表未更新？）", CRID_LOG_PART_NAME);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_part->size < 16 * SECTOR_SIZE) {
        ESP_LOGW(TAG, "%s 分区太小（%u 字节）", CRID_LOG_PART_NAME, (unsigned)s_part->size);
        return ESP_ERR_INVALID_SIZE;
    }

    s_secs = (uint16_t)(s_part->size / SECTOR_SIZE - 1);

    // 1) meta
    log_meta_t m;
    memset(&m, 0, sizeof(m));
    bool meta_ok = false;
    if (esp_partition_read(s_part, 0, &m, sizeof(m)) == ESP_OK) {
        meta_ok = (m.magic == META_MAGIC &&
                   m.crc == crc16(&m, offsetof(log_meta_t, crc)) &&
                   m.gen != 0);
    }
    if (!meta_ok) {
        memset(&m, 0, sizeof(m));
        m.magic = META_MAGIC;
        m.gen   = 1;
        m.crc   = crc16(&m, offsetof(log_meta_t, crc));
        esp_partition_erase_range(s_part, 0, SECTOR_SIZE);
        esp_partition_write(s_part, 0, &m, sizeof(m));
        ESP_LOGI(TAG, "日志分区首次使用，已建立 meta（gen=1）");
    }
    s_gen = m.gen;

    s_mux = xSemaphoreCreateMutex();
    s_q   = xQueueCreate(QUEUE_LEN, sizeof(crid_log_rec_t));
    if (s_mux == NULL || s_q == NULL) {
        ESP_LOGE(TAG, "创建队列/互斥量失败");
        return ESP_ERR_NO_MEM;
    }

    // 2) 扫描各扇区首记录，定位最后写入位置
    uint32_t max_seq = 0;
    int best = -1;
    int valid_secs = 0;
    for (uint16_t i = 0; i < s_secs; i++) {
        crid_log_rec_t r;
        if (esp_partition_read(s_part, sec_off(i), &r, CRID_LOG_REC_SIZE) != ESP_OK) continue;
        if (!rec_ok(&r)) continue;
        valid_secs++;
        if (best < 0 || r.seq >= max_seq) {
            max_seq = r.seq;
            best    = i;
        }
    }

    if (best < 0) {
        s_wsec = 0;
        s_wslot = 0;
        s_seq  = 1;
        s_count = 0;
    } else {
        int slot = PER_SEC;
        for (int k = 0; k < PER_SEC; k++) {
            crid_log_rec_t r;
            if (esp_partition_read(s_part, sec_off((uint16_t)best) + (uint32_t)k * CRID_LOG_REC_SIZE,
                                   &r, CRID_LOG_REC_SIZE) != ESP_OK || !rec_ok(&r)) {
                slot = k;
                break;
            }
        }
        s_seq = max_seq + 1;
        if (slot >= PER_SEC) {
            s_wsec  = (uint16_t)((best + 1) % s_secs);
            s_wslot = 0;
        } else {
            s_wsec  = (uint16_t)best;
            s_wslot = (uint16_t)slot;
        }
        // 已写条数：除最后一个扇区外都视为写满
        s_count = (uint32_t)(valid_secs - 1) * PER_SEC + (uint32_t)(slot >= PER_SEC ? PER_SEC : slot);
        if (s_count > capacity()) s_count = capacity();
        if (s_count) {
            // 用最近若干条重建目标表（重启后网页仍有目标列表）
            uint32_t n = s_count < SEED_SCAN ? s_count : SEED_SCAN;
            uint32_t na = newest_abs();
            for (uint32_t i = 0; i < n; i++) {
                crid_log_rec_t r;
                if (read_abs((na + capacity() - i) % capacity(), &r)) {
                    target_update(&r, false);
                }
            }
        }
    }

#ifndef CRID_LOG_HOST_TEST
    if (xTaskCreate(log_task, "crid_log", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建写任务失败");
        return ESP_ERR_NO_MEM;
    }
#endif

    s_ready = true;
    ESP_LOGI(TAG, "轨迹日志就绪：%u 扇区 / %u 条容量，已存 %u 条，写指针 %u/%u",
             s_secs, (unsigned)capacity(), (unsigned)s_count, s_wsec, s_wslot);
    return ESP_OK;
}

bool crid_log_ready(void) {
    return s_ready;
}

void crid_log_append(const crid_log_rec_t *r) {
    if (!s_ready || r == NULL) return;
    if (xQueueSend(s_q, r, 0) != pdTRUE) s_dropped++;
}

void crid_log_get_stats(crid_log_stats_t *out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (!s_ready) return;

    out->ready       = true;
    out->capacity    = capacity();
    out->total_bytes = (uint32_t)s_secs * SECTOR_SIZE;
    out->count       = s_count;
    out->dropped     = s_dropped;
    out->used_bytes  = (s_count >= out->capacity)
                       ? out->total_bytes
                       : (((s_count + PER_SEC - 1) / PER_SEC) * SECTOR_SIZE);

    if (s_count) {
        crid_log_rec_t r;
        uint32_t na = newest_abs();
        if (read_abs(na, &r)) {
            out->newest_unix = r.t_unix;
            out->newest_tick = r.tick_ms;
        }
        uint32_t oldest = (na + capacity() - (s_count - 1)) % capacity();
        if (read_abs(oldest, &r)) {
            out->oldest_unix = r.t_unix;
            out->oldest_tick = r.tick_ms;
        }
    }
}

int crid_log_targets(crid_log_target_t *out, int max) {
    if (!s_ready || out == NULL || max <= 0) return 0;
    int n = s_tgn < max ? s_tgn : max;
    // 按最后出现时间倒序（插入排序，最多 16 项）
    crid_log_target_t tmp[MAX_TARGETS];
    int m = s_tgn;
    memcpy(tmp, s_tg, sizeof(crid_log_target_t) * (size_t)m);
    for (int i = 1; i < m; i++) {
        crid_log_target_t k = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].newest_tick < k.newest_tick) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = k;
    }
    memcpy(out, tmp, sizeof(crid_log_target_t) * (size_t)n);
    return n;
}

int crid_log_points(const uint8_t mac[6], uint32_t limit,
                    crid_log_rec_t *out, uint32_t scan_max) {
    if (!s_ready || out == NULL || limit == 0 || s_count == 0) return 0;

    uint32_t cap = capacity();
    uint32_t na  = newest_abs();
    uint32_t n   = 0;
    uint32_t lim = limit < s_count ? limit : s_count;
    if (scan_max == 0 || scan_max > s_count) scan_max = s_count;

    for (uint32_t i = 0; i < scan_max && n < lim; i++) {
        crid_log_rec_t r;
        if (!read_abs((na + cap - i) % cap, &r)) continue;
        if (mac != NULL && memcmp(r.mac, mac, 6) != 0) continue;
        out[n++] = r;
    }
    return (int)n;
}

void crid_log_foreach(crid_log_cb_t cb, void *ctx) {
    if (!s_ready || cb == NULL) return;
    // 当前写扇区就是环形里最旧的那一段，从它开始绕一圈即时间顺序
    uint16_t start = s_wsec;
    for (uint16_t i = 0; i < s_secs; i++) {
        uint16_t sec = (uint16_t)((start + i) % s_secs);
        if (esp_partition_read(s_part, sec_off(sec), s_sbuf, SECTOR_SIZE) != ESP_OK) continue;
        for (int k = 0; k < PER_SEC; k++) {
            crid_log_rec_t *r = (crid_log_rec_t *)(void *)(s_sbuf + (size_t)k * CRID_LOG_REC_SIZE);
            if (!rec_ok(r)) continue;
            if (!cb(r, ctx)) return;
        }
    }
}

esp_err_t crid_log_clear(void) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(3000)) != pdTRUE) return ESP_ERR_TIMEOUT;

    log_meta_t m;
    memset(&m, 0, sizeof(m));
    m.magic = META_MAGIC;
    m.gen   = (uint16_t)(s_gen + 1);
    if (m.gen == 0) m.gen = 1;
    m.crc   = crc16(&m, offsetof(log_meta_t, crc));

    // 只擦 meta 扇区：旧记录的 gen 对不上，扫描时全部视为无效；
    // 随后写入会按环形顺序擦除各自的扇区，旧数据自然被覆盖。
    esp_err_t err = esp_partition_erase_range(s_part, 0, SECTOR_SIZE);
    if (err == ESP_OK) err = esp_partition_write(s_part, 0, &m, sizeof(m));
    if (err == ESP_OK) {
        s_gen   = m.gen;
        s_wsec  = 0;
        s_wslot = 0;
        s_count = 0;
        s_tgn   = 0;
        s_seq++;
        ESP_LOGI(TAG, "轨迹记录已清空（gen=%u）", s_gen);
    } else {
        ESP_LOGE(TAG, "清空失败: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_mux);
    return err;
}
