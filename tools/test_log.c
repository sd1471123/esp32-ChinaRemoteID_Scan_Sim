/**
 * @file test_log.c
 * @brief crid_log 环形日志的主机端单元测试（不依赖 ESP32）
 *
 * 把 crid_log.c 直接 include 进来，用一块 64 KB 的 RAM 模拟 flash，
 * 严格模拟「写只能 1→0、擦除整扇区填 0xFF」的语义，所以只要写之前忘了擦，
 * 就会立刻报 WRITE-VIOLATION。
 *
 * 编译运行：
 *   cd tools && gcc -std=c11 -Wall -Wextra -Ihosttest -o /tmp/test_log test_log.c && /tmp/test_log
 */

#define CRID_LOG_HOST_TEST

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../main/crid_log.c"

/* ---------------- flash 模拟 ---------------- */

#define PART_SIZE (64 * 1024)          // 16 扇区：1 meta + 15 数据（容量 960 条）
#define SECTORS   (PART_SIZE / 4096)

static uint8_t         g_flash[PART_SIZE];
static esp_partition_t g_part = { PART_SIZE, 0x620000, "ridlog" };
static int             g_write_violations = 0;
static int             g_erase_calls = 0;

const char *esp_err_to_name(esp_err_t e) {
    static char b[24];
    snprintf(b, sizeof b, "err(%d)", e);
    return b;
}

const esp_partition_t *esp_partition_find_first(uint8_t type, uint8_t subtype,
                                                const char *label) {
    (void)type; (void)subtype; (void)label;
    return &g_part;
}

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t n) {
    if (p == NULL || off + n > p->size) return ESP_ERR_INVALID_ARG;
    memcpy(dst, g_flash + off, n);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t n) {
    if (p == NULL || off + n > p->size) return ESP_ERR_INVALID_ARG;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        if ((g_flash[off + i] & s[i]) != s[i]) {
            g_write_violations++;
            fprintf(stderr, "WRITE-VIOLATION off=0x%zx old=0x%02x new=0x%02x\n",
                    off + i, g_flash[off + i], s[i]);
            return ESP_FAIL;
        }
    }
    memcpy(g_flash + off, src, n);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t n) {
    if (p == NULL || off + n > p->size) return ESP_ERR_INVALID_ARG;
    if ((off % 4096) || (n % 4096)) return ESP_ERR_INVALID_ARG;
    memset(g_flash + off, 0xFF, n);
    g_erase_calls++;
    return ESP_OK;
}

/* ---------------- FreeRTOS 桩 ---------------- */

QueueHandle_t xQueueCreate(unsigned l, unsigned s) { (void)l; (void)s; return (void *)1; }
int xQueueSend(QueueHandle_t q, const void *i, TickType_t w) { (void)q; (void)i; (void)w; return pdTRUE; }
int xQueueReceive(QueueHandle_t q, void *i, TickType_t w) { (void)q; (void)i; (void)w; return pdFALSE; }
void vQueueDelete(QueueHandle_t q) { (void)q; }

int xTaskCreate(void (*fn)(void *), const char *n, unsigned st, void *a,
                unsigned p, TaskHandle_t *h) {
    (void)fn; (void)n; (void)st; (void)a; (void)p; (void)h;
    return pdPASS;   // 不真正起线程：测试直接调 store_one
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t w) { (void)s; (void)w; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }

/* ---------------- 测试工具 ---------------- */

static int g_fail = 0;

static void check(bool cond, const char *msg) {
    if (cond) {
        printf("  ok   %s\n", msg);
    } else {
        printf("  FAIL %s\n", msg);
        g_fail++;
    }
}

static crid_log_rec_t mkrec(uint32_t tick, int32_t lat, uint8_t mac_tail, const char *uid) {
    crid_log_rec_t r;
    memset(&r, 0, sizeof r);
    r.tick_ms   = tick;
    r.t_unix    = 1700000000u + tick / 1000;
    r.mac[0]    = 0xAA;
    r.mac[5]    = mac_tail;
    r.lat_e7    = lat;
    r.lon_e7    = lat * 2;
    r.alt_m     = 120;
    r.spd_h_dms = 55;
    r.trk_ddeg  = 900;
    r.ua_type   = 2;
    r.status    = 2;
    r.proto     = 4;
    r.rssi      = -60;
    r.flags     = CRID_LOG_F_FIX;
    if (uid) strncpy(r.uid, uid, sizeof r.uid);
    return r;
}

// 模拟掉电重启：清掉所有 RAM 状态，flash 内容保留
static void sim_reboot(void) {
    s_ready = false;
    if (s_mux) { vSemaphoreDelete(s_mux); s_mux = NULL; }
    if (s_q)   { vQueueDelete(s_q);        s_q   = NULL; }
    s_tgn = 0;
    s_dropped = 0;
    s_gen = 0;
    s_seq = 0;
    s_wsec = 0;
    s_wslot = 0;
    s_count = 0;
    memset(s_tg, 0, sizeof s_tg);
}

static void store(crid_log_rec_t *r) {
    xSemaphoreTake(s_mux, 0);
    store_one(r);
    xSemaphoreGive(s_mux);
}

/* foreach 收集器 */
typedef struct {
    uint32_t n;
    uint32_t first_seq;
    uint32_t last_seq;
    bool     ascending;
} collect_t;

static bool collect_cb(const crid_log_rec_t *r, void *ctx) {
    collect_t *c = (collect_t *)ctx;
    if (c->n == 0) c->first_seq = r->seq;
    else if (r->seq <= c->last_seq) c->ascending = false;
    c->last_seq = r->seq;
    c->n++;
    return true;
}

static crid_log_rec_t g_big[1000] __attribute__((aligned(4)));

int main(void) {
    memset(g_flash, 0xFF, sizeof g_flash);

    printf("[1] 空分区初始化\n");
    check(crid_log_init() == ESP_OK, "init 返回 OK");
    check(crid_log_ready(), "模块 ready");
    crid_log_stats_t st;
    crid_log_get_stats(&st);
    check(st.count == 0, "空分区 count=0");
    check(st.capacity == 960, "容量 = 15 扇区 × 64 条 = 960");

    printf("[2] 写 10 条\n");
    for (int i = 0; i < 10; i++) {
        crid_log_rec_t r = mkrec(1000 + (uint32_t)i * 100, 1000 + i, 0x01, "TESTID0001");
        store(&r);
    }
    crid_log_get_stats(&st);
    check(st.count == 10, "count=10");
    crid_log_rec_t buf[32] __attribute__((aligned(4)));
    int n = crid_log_points(NULL, 32, buf, 0);
    check(n == 10, "points 返回 10 条");
    check(buf[0].seq == 10 && buf[9].seq == 1, "按新到旧排序（seq 10→1）");
    check(buf[0].lat_e7 == 1009, "最新点坐标正确");
    check(buf[0].crc != 0, "记录带 CRC");

    printf("[3] 按 MAC 过滤\n");
    for (int i = 0; i < 3; i++) {
        crid_log_rec_t r = mkrec(5000 + (uint32_t)i, 9999, 0x02, "OTHER00002");
        store(&r);
    }
    const uint8_t mac2[6] = { 0xAA, 0, 0, 0, 0, 0x02 };
    n = crid_log_points(mac2, 32, buf, 0);
    check(n == 3, "只取到该 MAC 的 3 条");
    n = crid_log_points(NULL, 32, buf, 0);
    check(n == 13, "全部 13 条");

    printf("[4] 模拟掉电重启\n");
    sim_reboot();
    check(crid_log_init() == ESP_OK, "重启 init 成功");
    crid_log_get_stats(&st);
    check(st.count == 13, "重启后仍认到 13 条");
    n = crid_log_points(NULL, 32, buf, 0);
    check(n == 13 && buf[0].seq == 13, "重启后最新 seq=13");
    crid_log_target_t tg[8];
    int nt = crid_log_targets(tg, 8);
    check(nt == 2, "重启后目标表恢复出 2 个目标");
    check(nt == 2 && memcmp(tg[0].mac, mac2, 6) == 0, "目标按最后出现时间倒序");

    printf("[5] 写满一圈并溢出（容量 960）\n");
    for (int i = 0; i < 1200; i++) {
        crid_log_rec_t r = mkrec(10000 + (uint32_t)i, 2000 + i, 0x03, "OVER000003");
        store(&r);
    }
    crid_log_get_stats(&st);
    check(st.count == 960, "count 封顶 960");
    check(st.used_bytes == st.total_bytes, "占用达到分区总容量");
    n = crid_log_points(NULL, 1000, g_big, 0);
    check(n == 960, "取回 960 条");
    // 共写入 13 + 1200 = 1213 条，seq 14..1213 里最新的 960 条 = 254..1213
    check(g_big[0].seq == 1213, "最新 seq=1213");
    check(g_big[959].seq == 254, "最旧 seq=254（更早的被覆盖）");
    check(g_big[959].tick_ms == 10240, "最旧那条的 tick 正确（10240）");

    printf("[6] 顺序遍历\n");
    collect_t c = { 0, 0, 0, true };
    crid_log_foreach(collect_cb, &c);
    check(c.n == 960, "遍历到 960 条");
    check(c.ascending, "遍历顺序按 seq 递增（旧→新）");
    check(c.first_seq == 254 && c.last_seq == 1213, "首尾 seq 正确");

    printf("[7] 清空\n");
    check(crid_log_clear() == ESP_OK, "clear 返回 OK");
    crid_log_get_stats(&st);
    check(st.count == 0, "清空后 count=0");
    n = crid_log_points(NULL, 32, buf, 0);
    check(n == 0, "清空后读不到旧数据");
    c = (collect_t){ 0, 0, 0, true };
    crid_log_foreach(collect_cb, &c);
    check(c.n == 0, "清空后遍历为空");

    for (int i = 0; i < 3; i++) {
        crid_log_rec_t r = mkrec(90000 + (uint32_t)i, 7000 + i, 0x04, "AFTERCLR01");
        store(&r);
    }
    crid_log_get_stats(&st);
    check(st.count == 3, "清空后新写 3 条");
    n = crid_log_points(NULL, 32, buf, 0);
    check(n == 3 && buf[0].lat_e7 == 7002, "新记录可正常读取");

    printf("[8] 清空后再重启（代次持久化）\n");
    sim_reboot();
    check(crid_log_init() == ESP_OK, "再次 init");
    crid_log_get_stats(&st);
    check(st.count == 3, "重启后仍是 3 条（清空前的数据没有复活）");
    crid_log_rec_t r = mkrec(95000, 8100, 0x05, "POSTREBT01");
    store(&r);
    crid_log_get_stats(&st);
    check(st.count == 4, "重启后继续追加正常");

    printf("[9] flash 写入合规\n");
    printf("    擦除次数 %d\n", g_erase_calls);
    check(g_write_violations == 0, "没有出现「未擦先写」的违规");

    printf("\n=== %s（失败 %d 项）===\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
