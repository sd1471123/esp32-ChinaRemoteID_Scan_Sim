#include "crid_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CN_C-RID_WIFI";

// 毫秒 -> 802.11 时间单元（TU）。ESP-IDF 的 beacon_interval 以 TU 为单位
// （1 TU = 1024 µs），若直接把毫秒数写进去，实际间隔会放大约 2.4%。
// 这里向下取整，使实际间隔略短于设定值，避免 1000ms 被取整成 1000.4ms
// 而跌破 GB 42590 表 A.11「固定信道 ≥1Hz」的下限。
static uint16_t crid_ms_to_tu(uint16_t ms) {
    uint32_t tu = (uint32_t)((float)ms / 1.024f);
    // esp_wifi_types 规定 beacon_interval 范围为 100~60000 TU，
    // 越界会让 esp_wifi_set_config 直接返回 ESP_ERR_INVALID_ARG
    if (tu < 100) tu = 100;
    if (tu > 60000) tu = 60000;
    return (uint16_t)tu;
}

// 说明：早期版本这里还有一个 STA 模式的 crid_wifi_init()，用于纯接收。
// 现在统一为 softAP + 可选混杂监听（AP 模式下混杂同样可用），
// 手机上始终能连热点进网页，因此该函数已移除。

/**
 * @brief 切换射频工作频段
 *
 * ESP32-C5 只有一个射频链路，2.4G 与 5.8G 互斥。IDF 要求该 API 在
 * esp_wifi_start() 之后调用，切换后信道会回落到该频段的默认值，
 * 需要再调 crid_wifi_update_ap() 把 SSID/信道/间隔重新写回去。
 */
static esp_err_t crid_wifi_apply_band(uint8_t band) {
#if CRID_HAS_5G_BAND
    wifi_band_mode_t mode = (band == CRID_BAND_5G) ? WIFI_BAND_MODE_5G_ONLY
                                                   : WIFI_BAND_MODE_2G_ONLY;
    esp_err_t ret = esp_wifi_set_band_mode(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set band mode %d failed: %s", (int)mode, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "射频频段: %s", band == CRID_BAND_5G ? "5.8G" : "2.4G");
    return ESP_OK;
#else
    if (band == CRID_BAND_5G) {
        ESP_LOGW(TAG, "本芯片无 5 GHz 射频，强制使用 2.4G");
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
#endif
}

esp_err_t crid_wifi_init_ap(uint8_t band, uint8_t channel, const char *ssid, uint16_t beacon_interval_ms) {
    esp_err_t ret;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 使用真实 softAP 模式：硬件自动发送 Beacon（每 beacon_interval ms 一次）并
    // 应答手机/接收端的 Probe Request，保证 WiFi 列表中稳定可见
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 国家码决定 5G 可用信道集合：CN 允许 36-64 与 149-165。
    // 默认的 "01"(world safe) 会裁掉大部分 5G 信道，必须显式指定。
    ret = esp_wifi_set_country_code("CN", true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set country CN failed: %s", esp_err_to_name(ret));
    }

    // 创建 AP netif（含 DHCP 服务），手机连接后才能获取 192.168.4.x 地址
    esp_netif_t *netif_ap = esp_netif_create_default_wifi_ap();
    if (netif_ap == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        return ESP_FAIL;
    }

    // 先 start 再设置配置：esp_wifi_set_band_mode() 要求 Wi-Fi 已启动，
    // 而 C5 复位后默认 band mode 是 5G_only，此时写入 2.4G 信道会被驱动
    // 直接拒绝（Channel number 6 (2G) is not supported in current band mode）。
    // 所以顺序必须是 start -> 切 band -> set_config(匹配该频段的信道)。
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (crid_wifi_apply_band(band) != ESP_OK) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    wifi_config_t ap_config = { 0 };
    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(ap_config.ap.ssid)) ssid_len = sizeof(ap_config.ap.ssid);
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = (uint8_t)ssid_len;
    ap_config.ap.channel = crid_band_normalize_channel(band, channel);
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    ap_config.ap.ssid_hidden = 0;
    // Beacon 间隔与 RID 帧更新周期一致，保证每帧携带最新计数器
    // 注意：ESP-IDF 的 beacon_interval 单位是 TU（1 TU = 1.024 ms），不是毫秒
    ap_config.ap.beacon_interval = crid_ms_to_tu(beacon_interval_ms);

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 5.8G 只支持 20 MHz 带宽，显式设置避免驱动在 5G 上尝试 40 MHz
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);

    ESP_LOGI(TAG, "Wi-Fi AP initialized: ssid=%s, band=%s, channel=%u, beacon=%ums (%u TU)",
             ssid, band == CRID_BAND_5G ? "5.8G" : "2.4G", channel,
             beacon_interval_ms, crid_ms_to_tu(beacon_interval_ms));
    return ESP_OK;
}

esp_err_t crid_wifi_update_ap(const char *ssid, uint8_t channel, uint16_t beacon_interval_ms) {
    wifi_config_t ap_config = { 0 };
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(ap_config.ap.ssid)) ssid_len = sizeof(ap_config.ap.ssid);
    memset(ap_config.ap.ssid, 0, sizeof(ap_config.ap.ssid));
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = (uint8_t)ssid_len;
    ap_config.ap.channel = channel;
    ap_config.ap.beacon_interval = crid_ms_to_tu(beacon_interval_ms);

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi AP config updated: ssid=%s, channel=%u, beacon=%ums (%u TU)",
                 ssid, channel, beacon_interval_ms, crid_ms_to_tu(beacon_interval_ms));
    } else {
        ESP_LOGE(TAG, "Wi-Fi AP config update failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief 运行时切换频段并重建热点（网页保存频段时调用）
 *
 * 单射频：切频段会重建 AP，所有已连接客户端（含正在提交表单的手机）都会掉线。
 * 调用前必须先把手头的 HTTP 响应发出去。
 */
esp_err_t crid_wifi_switch_band(uint8_t band, const char *ssid, uint8_t channel,
                                uint16_t beacon_interval_ms) {
    esp_err_t ret = crid_wifi_apply_band(band);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));   // 等驱动完成频段切换
    return crid_wifi_update_ap(ssid, channel, beacon_interval_ms);
}

esp_err_t crid_wifi_set_rid_ie(uint8_t slot, const uint8_t *ie, uint16_t len) {
    if (ie == NULL || len < 2 || ie[0] != 0xDD || len != (uint16_t)ie[1] + 2) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_vendor_ie_id_t id = (slot == 1) ? WIFI_VND_IE_ID_1 : WIFI_VND_IE_ID_0;

    // 同时挂到 Beacon 和 Probe Response，主动扫描也能拿到 RID 数据。
    // 注意: IDF 不允许直接覆盖已设置的 IE 槽位，必须先清除(enable=false)再设置。
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, id, ie);
    esp_err_t ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, id, ie);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set beacon vendor IE failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, id, ie);
    ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, id, ie);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set probe-resp vendor IE failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

void crid_wifi_clear_rid_ie(uint8_t slot) {
    wifi_vendor_ie_id_t id = (slot == 1) ? WIFI_VND_IE_ID_1 : WIFI_VND_IE_ID_0;
    // enable=false 的清除调用允许传入 NULL 数据（IDF 仅校验 enable 时的指针）
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, id, NULL);
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, id, NULL);
}

esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len) {
    if (frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 每次发送前手动递增 802.11 Sequence Control（帧偏移 22/23 字节）。
    // 若所有 Beacon 帧都使用固定 SeqNum=0，接收端会按协议做重复帧过滤，
    // 把后续 Beacon 当作重传丢弃，导致 RID 信号时有时无。
    static uint16_t tx_seq = 0;
    uint16_t seq = tx_seq++ & 0x0FFF;   // SeqNum 12 位循环
    // Sequence Control 小端序: 低字节 = FragNum(4bit) | SeqNum 低 4bit，高字节 = SeqNum 高 8bit
    uint8_t *mutable_frame = (uint8_t *)frame;
    mutable_frame[22] = (uint8_t)((seq & 0x0F) << 4);
    mutable_frame[23] = (uint8_t)((seq >> 4) & 0xFF);

    // 四级 fallback: STA/AP × (no-seq / with-seq)
    static const struct {
        wifi_interface_t iface;
        bool enable_seq;
        const char *desc;
    } fallbacks[] = {
        {WIFI_IF_STA, false, "STA no seq"},
        {WIFI_IF_STA, true,  "STA with seq"},
        {WIFI_IF_AP,  false, "AP no seq"},
        {WIFI_IF_AP,  true,  "AP with seq"},
    };

    esp_err_t ret = ESP_FAIL;
    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
        ret = esp_wifi_80211_tx(fallbacks[i].iface, frame, len, fallbacks[i].enable_seq);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "TX OK (%s)", fallbacks[i].desc);
            return ESP_OK;
        }
        ESP_LOGD(TAG, "TX attempt %s: %s", fallbacks[i].desc, esp_err_to_name(ret));
    }

    ESP_LOGE(TAG, "All TX methods failed");
    return ret;
}
