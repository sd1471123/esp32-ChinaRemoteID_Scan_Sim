/**
 * @file crid_time.c
 * @brief 全局时间基准（浏览器授时 + 单调时钟）
 *
 * 用 esp_timer 的 64 位单调计数做基准，而不是 esp_log_timestamp()：
 * 后者是 uint32 毫秒，约 49.7 天回绕，长期挂机后时间会突然倒退十年。
 */

#include "crid_time.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "CN_C-RID_TIME";

// 0 = 未校准。校准后 = 真实 epoch - 单调毫秒，靠两者相加还原绝对时间。
static int64_t s_epoch_offset_ms = 0;

static inline int64_t mono_ms(void) {
    return esp_timer_get_time() / 1000;
}

bool crid_time_set_epoch_ms(uint64_t epoch_ms) {
    if (epoch_ms == 0) {
        s_epoch_offset_ms = 0;
        ESP_LOGW(TAG, "时间校准已清除，绝对时间字段将按「未知」发送");
        return true;
    }

    // 合理区间：2023-01-01 ~ 2100-01-01。浏览器刚开机/时区错乱时会给出
    // 1970 附近或极远的数值，这种授时比不授时更糟（会把磁盘日志和报文的
    // 时间一起带偏），直接拒绝。
    if (epoch_ms < 1672531200000ULL || epoch_ms > 4102444800000ULL) {
        ESP_LOGW(TAG, "拒绝异常授时值 %llu ms", (unsigned long long)epoch_ms);
        return false;
    }

    s_epoch_offset_ms = (int64_t)epoch_ms - mono_ms();
    ESP_LOGI(TAG, "系统时间已校准: %llu ms（偏移 %lld ms）",
             (unsigned long long)epoch_ms, (long long)s_epoch_offset_ms);
    return true;
}

uint64_t crid_time_epoch_ms(void) {
    if (s_epoch_offset_ms == 0) return 0;
    int64_t v = mono_ms() + s_epoch_offset_ms;
    return (v > 0) ? (uint64_t)v : 0;
}
