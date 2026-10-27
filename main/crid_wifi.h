#ifndef CRID_WIFI_H
#define CRID_WIFI_H

#include "crid_config.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 以 softAP 模式初始化 Wi-Fi（真实 Beacon + 应答探测请求，列表中稳定可见）
 *
 * 同时创建 AP netif（含 DHCP），手机连接后可获取 192.168.4.x 地址访问配置页面。
 * AP 始终运行，与「发射/接收」开关无关，保证任何配置下网页都能访问。
 *
 * @param band 射频工作频段 (CRID_BAND_2G / CRID_BAND_5G)
 * @param channel Wi-Fi 信道 (2.4G: 1-13, 5.8G: 149-165)
 * @param ssid 热点名称
 * @param beacon_interval_ms Beacon 广播间隔 (100-60000)
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_init_ap(uint8_t band, uint8_t channel, const char *ssid, uint16_t beacon_interval_ms);

/**
 * @brief 运行时更新 softAP 配置（SSID / 信道 / Beacon 间隔，来自网页保存）
 * @param ssid 新热点名称
 * @param channel 新信道（须与当前 band 匹配）
 * @param beacon_interval_ms 新广播间隔
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_update_ap(const char *ssid, uint8_t channel, uint16_t beacon_interval_ms);

/**
 * @brief 运行时切换频段并重建热点（2.4G <-> 5.8G）
 *
 * 单射频：切换会重建 AP，已连接的手机必然掉线。调用方必须先把 HTTP 响应发完。
 *
 * @param band 目标频段
 * @param ssid 热点名称
 * @param channel 目标频段下的信道
 * @param beacon_interval_ms 广播间隔
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_switch_band(uint8_t band, const char *ssid, uint8_t channel,
                                uint16_t beacon_interval_ms);

/**
 * @brief 将 RID 厂商 IE 挂到 softAP 的 Beacon / Probe Response（指定槽位）
 *
 * IE 格式: [0xDD][len][OUI FA0BBC][Type 0x0D][payload...]，
 * GB42590 打包报文与 GB46750 位图报文各占一个槽位（ID_0 / ID_1）。
 *
 * @param slot IE 槽位 (0=WIFI_VND_IE_ID_0, 1=WIFI_VND_IE_ID_1)
 * @param ie 完整 IE 数据
 * @param len IE 总长度
 * @return ESP_OK 成功
 */
esp_err_t crid_wifi_set_rid_ie(uint8_t slot, const uint8_t *ie, uint16_t len);

/**
 * @brief 清除指定槽位的 RID 厂商 IE（从 Beacon / Probe Response 摘除）
 * @param slot IE 槽位 (0 或 1)
 */
void crid_wifi_clear_rid_ie(uint8_t slot);

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
