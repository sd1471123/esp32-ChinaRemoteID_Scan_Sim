#ifndef CRID_WIFI_H
#define CRID_WIFI_H

#include "crid_config.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Wi-Fi 用于 raw 802.11 帧发送
 * @param channel Wi-Fi 信道
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_init(uint8_t channel);

/**
 * @brief 以 softAP 模式初始化 Wi-Fi（真实 Beacon + 应答探测请求，列表中稳定可见）
 * @param channel Wi-Fi 信道
 * @param ssid 热点名称
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_init_ap(uint8_t channel, const char *ssid);

/**
 * @brief 将 RID 厂商 IE 挂到 softAP 的 Beacon / Probe Response
 *
 * IE 格式: [0xDD][len][OUI FA0BBC][Type 0x0D][Counter][Packed 128B]，共 135 字节，
 * 由 crid_build_vendor_ie() 构建。
 *
 * @param ie 完整 IE 数据
 * @param len IE 总长度（135）
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_set_rid_ie(const uint8_t *ie, uint16_t len);

/**
 * @brief 发送 raw 802.11 帧，带 fallback 机制
 * @param frame 帧数据
 * @param len 帧长度
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // CRID_WIFI_H
