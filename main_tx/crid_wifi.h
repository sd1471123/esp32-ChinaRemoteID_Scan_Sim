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
 *
 * 同时创建 AP netif（含 DHCP），手机连接后可获取 192.168.4.x 地址访问配置页面。
 *
 * @param channel Wi-Fi 信道
 * @param ssid 热点名称
 * @param beacon_interval_ms Beacon 广播间隔 (100-60000)
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_init_ap(uint8_t channel, const char *ssid, uint16_t beacon_interval_ms);

/**
 * @brief 运行时更新 softAP 配置（SSID / 信道 / Beacon 间隔，来自网页保存）
 * @param ssid 新热点名称
 * @param channel 新信道 (1-13)
 * @param beacon_interval_ms 新广播间隔
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_update_ap(const char *ssid, uint8_t channel, uint16_t beacon_interval_ms);

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
