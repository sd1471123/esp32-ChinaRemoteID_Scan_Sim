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
#include <stdlib.h>

#include "esp_log.h"
#include "esp_partition.h"
#ifndef CRID_LOG_HOST_TEST
#include "nvs.h"      // 主机端单元测试没有 NVS，墓碑表在那里退化成纯 RAM
#endif
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
static volatile bool s_enabled;   // 记录开关：默认关闭，用户拨一次即落 NVS，重启沿用

static QueueHandle_t     s_q;
static SemaphoreHandle_t s_mux;
static crid_log_target_t s_tg[MAX_TARGETS];
static int               s_tgn;
// 遍历用扇区缓冲：不放栈（httpd 任务栈只有 8 KB），且要求 4 字节对齐
static uint8_t s_sbuf[SECTOR_SIZE] __attribute__((aligned(4)));

/* ---------------- 单目标删除的墓碑表 ----------------
 * 环形日志是追加写，没法就地删掉某个目标的记录（擦除最小单位是 4 KB 扇区，
 * 里面还混着别的目标）。所以删除做成「记一个 seq 上界」：该 MAC 的 seq 不超过
 * 上界的记录，在扫描/分页/导出时直接当不存在。之后这台无人机再飞回来，新记录
 * 的 seq 更大，不受影响。
 * 墓碑必须落 NVS —— 上电会从 flash 重建目标表，只放 RAM 的话重启后就会复活。
 */
#define DEL_NS   "cridlog"
#define DEL_KEY  "del"
#define EN_KEY   "en"
#define MAX_DEL  8

typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    uint16_t rsv;
    uint32_t cut;       // seq <= cut 的记录视为已删除
} del_ent_t;

static del_ent_t s_del[MAX_DEL];
static int       s_deln;

static void del_load(void) {
    s_deln = 0;
#ifdef CRID_LOG_HOST_TEST
    // 主机端没有 NVS：墓碑只活在内存里，测试正需要这种「重启即清空」
    return;
#else
    nvs_handle_t h;
    if (nvs_open(DEL_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = 0;
    if (nvs_get_blob(h, DEL_KEY, NULL, &len) == ESP_OK && len > 0 && len <= sizeof(s_del)) {
        if (nvs_get_blob(h, DEL_KEY, s_del, &len) == ESP_OK) {
            s_deln = (int)(len / sizeof(del_ent_t));
            if (s_deln > MAX_DEL) s_deln = MAX_DEL;
        }
    }
    nvs_close(h);
    if (s_deln) ESP_LOGI(TAG, "载入 %d 个已删除目标的墓碑", s_deln);
#endif
}

static esp_err_t del_save(void) {
#ifdef CRID_LOG_HOST_TEST
    return ESP_OK;
#else
    nvs_handle_t h;
    esp_err_t e = nvs_open(DEL_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, DEL_KEY, s_del, (size_t)s_deln * sizeof(del_ent_t));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
#endif
}

// 记录开关持久化：用户每拨一次就落 NVS，下次上电沿用上次的选择。
// 出厂/擦除后没有这个键 ⇒ 默认关闭，符合「不授时就不写记录」的原则。
static bool en_load(void) {
#ifdef CRID_LOG_HOST_TEST
    return false;
#else
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(DEL_NS, NVS_READONLY, &h) != ESP_OK) return false;
    if (nvs_get_u8(h, EN_KEY, &v) != ESP_OK) v = 0;
    nvs_close(h);
    return v != 0;
#endif
}

static void en_save(bool on) {
#ifdef CRID_LOG_HOST_TEST
    (void)on;
#else
    nvs_handle_t h;
    if (nvs_open(DEL_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, EN_KEY, on ? 1 : 0) == ESP_OK) nvs_commit(h);
    nvs_close(h);
#endif
}

static bool del_hit(const uint8_t mac[6], uint32_t seq) {
    for (int i = 0; i < s_deln; i++) {
        if (memcmp(s_del[i].mac, mac, 6) == 0 && seq <= s_del[i].cut) return true;
    }
    return false;
}

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

// 存活判定：物理上有效，且没被「单目标删除」的墓碑标记掉。
// 只有读路径用它；定位写指针那几处必须用 rec_ok（被删的记录仍然占着槽位）。
static bool rec_live(const crid_log_rec_t *r) {
    return rec_ok(r) && !del_hit(r->mac, r->seq);
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
    if (!rec_live(&tmp)) return false;
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
    del_load();     // 先载入墓碑，后面重建目标表时才知道哪些记录已被删掉
    s_enabled = en_load();   // 沿用上次用户拨的开关；没有记录则默认关闭

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

void crid_log_set_enabled(bool on) {
    if (s_enabled == on) return;      // 值没变就不碰 NVS，省擦写寿命
    s_enabled = on;
    en_save(on);
    ESP_LOGI(TAG, "记录%s（已保存，重启后保持）", on ? "已开启" : "已关闭");
}

bool crid_log_enabled(void) {
    return s_enabled;
}

void crid_log_append(const crid_log_rec_t *r) {
    if (!s_ready || r == NULL) return;
    if (!s_enabled) return;               // 开关关闭时不入队，rx 侧照常解包
    if (xQueueSend(s_q, r, 0) != pdTRUE) s_dropped++;
}

void crid_log_get_stats(crid_log_stats_t *out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (!s_ready) return;

    out->ready       = true;
    out->enabled     = s_enabled;
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

int crid_log_points(const uint8_t mac[6], uint32_t skip, uint32_t limit,
                    crid_log_rec_t *out, uint32_t scan_max, uint32_t *matched) {
    if (matched != NULL) *matched = 0;
    if (!s_ready || out == NULL || limit == 0 || s_count == 0) return 0;

    uint32_t cap = capacity();
    uint32_t na  = newest_abs();
    uint32_t n   = 0;
    uint32_t hit = 0;
    uint32_t lim = limit < s_count ? limit : s_count;
    if (scan_max == 0 || scan_max > s_count) scan_max = s_count;

    for (uint32_t i = 0; i < scan_max; i++) {
        crid_log_rec_t r;
        if (!read_abs((na + cap - i) % cap, &r)) continue;
        if (mac != NULL && memcmp(r.mac, mac, 6) != 0) continue;
        hit++;
        // skip 之前的是"更新的一页"，直接数过去但不拷出来
        if (hit > skip && n < lim) {
            out[n++] = r;
            // 不要求统计总数时，凑够一页就走人（分页只需 offset 时最省）
            if (matched == NULL && n >= lim) break;
        }
    }
    if (matched != NULL) *matched = hit;
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
            if (!rec_live(r)) continue;
            if (!cb(r, ctx)) return;
        }
    }
}

/* ---------------- 单目标删除 ---------------- */

// 按存活记录重算总条数与各目标计数（删除后调用，一次 4 MB 顺序读，约 0.1~0.5 s）
//
// 这里刻意不复用 s_sbuf：那块缓冲是 crid_log_foreach（CSV 导出）在用的，而 80 和
// 443 是两个独立的 httpd 任务，导出与删除完全可能真的同时在跑，共用缓冲会互相踩。
// 改用一次性 malloc 的 4 KB，用完就还；分配不到就退化成逐条读（慢一点，但正确）。
static void rebuild_locked(void) {
    s_tgn = 0;
    uint32_t n = 0;
    uint8_t *buf = (uint8_t *)malloc(SECTOR_SIZE);

    for (uint16_t i = 0; i < s_secs; i++) {
        if (buf != NULL) {
            if (esp_partition_read(s_part, sec_off(i), buf, SECTOR_SIZE) != ESP_OK) continue;
            for (int k = 0; k < PER_SEC; k++) {
                crid_log_rec_t *r = (crid_log_rec_t *)(void *)(buf + (size_t)k * CRID_LOG_REC_SIZE);
                if (!rec_live(r)) continue;
                n++;
                target_update(r, true);
            }
        } else {
            for (int k = 0; k < PER_SEC; k++) {
                crid_log_rec_t r;
                if (esp_partition_read(s_part, sec_off(i) + (uint32_t)k * CRID_LOG_REC_SIZE,
                                       &r, CRID_LOG_REC_SIZE) != ESP_OK) continue;
                if (!rec_live(&r)) continue;
                n++;
                target_update(&r, true);
            }
        }
    }
    if (buf != NULL) free(buf);
    s_count = n;
}

esp_err_t crid_log_delete_target(const uint8_t mac[6]) {
    if (!s_ready || mac == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;

    // 上界取「当前最大已用 seq」：此刻之前的记录全部作废，之后新写的仍然有效
    uint32_t cut = (s_seq > 0) ? (s_seq - 1) : 0;

    int slot = -1;
    for (int i = 0; i < s_deln; i++) {
        if (memcmp(s_del[i].mac, mac, 6) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_deln < MAX_DEL) {
            slot = s_deln++;
        } else {
            // 表满：淘汰上界最早（删得最久）的那条，保证最近删的总是有效
            slot = 0;
            for (int i = 1; i < s_deln; i++) {
                if (s_del[i].cut < s_del[slot].cut) slot = i;
            }
        }
    }
    memset(&s_del[slot], 0, sizeof(s_del[slot]));
    memcpy(s_del[slot].mac, mac, 6);
    s_del[slot].cut = cut;

    rebuild_locked();
    esp_err_t err = del_save();
    uint32_t left = s_count;
    xSemaphoreGive(s_mux);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "已删除目标 %02X:%02X:%02X:%02X:%02X:%02X 的历史记录（cut=%u），剩余 %u 条",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 (unsigned)cut, (unsigned)left);
    } else {
        ESP_LOGW(TAG, "删除标记已生效，但写入 NVS 失败: %s（重启后会恢复）", esp_err_to_name(err));
    }
    return err;
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
