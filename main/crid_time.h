#ifndef CRID_TIME_H
#define CRID_TIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 用浏览器上报的时间校准设备时钟
 *
 * 设备没有 RTC、也不联网，只能靠网页上报的 Date.now() 建立偏移。
 * 授时前所有「绝对时间」类字段一律按标准写「未知」，绝不拿开机计时
 * （1970-01-01 + uptime）冒充真实时间——否则 GB42590 的 System 报文
 * 时间戳会变成「1970 + uptime - 2019」的负数强转，解析出来是 2057 年，
 * 任何能解 System 报文的工具一眼就能看出是假数据。
 *
 * @param epoch_ms Unix 毫秒；0 = 清除校准，明显不合理的值会被拒绝
 * @return true = 已接受（含 0 的清除操作）；false = 值不合理被拒绝，时间基准保持原样
 */
bool crid_time_set_epoch_ms(uint64_t epoch_ms);

/**
 * @brief 当前 Unix 毫秒
 * @return 0 = 从未校准（调用方应写「未知」而不是 0 秒）
 */
uint64_t crid_time_epoch_ms(void);

/**
 * @brief 是否已校准
 */
static inline bool crid_time_synced(void) {
    return crid_time_epoch_ms() != 0;
}

#ifdef __cplusplus
}
#endif

#endif // CRID_TIME_H
