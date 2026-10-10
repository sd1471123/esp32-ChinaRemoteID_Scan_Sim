#include "crid_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CN_C-RID_WIFI";

esp_err_t crid_wifi_init(uint8_t channel) {
    esp_err_t ret;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 关闭调制解调器省电模式，避免射频休眠导致 raw 帧发送出现间隙
    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps failed: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_promiscuous failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi initialized: channel=%u, promiscuous mode", channel);
    return ESP_OK;
}

esp_err_t crid_wifi_init_ap(uint8_t channel, const char *ssid) {
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

    wifi_config_t ap_config = { 0 };
    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(ap_config.ap.ssid)) ssid_len = sizeof(ap_config.ap.ssid);
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = (uint8_t)ssid_len;
    ap_config.ap.channel = channel;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    ap_config.ap.ssid_hidden = 0;
    // Beacon 间隔与 RID 帧更新周期(1000ms)一致，保证每帧携带最新计数器
    ap_config.ap.beacon_interval = 1000;

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi AP initialized: ssid=%s, channel=%u, beacon=%ums",
             ssid, channel, ap_config.ap.beacon_interval);
    return ESP_OK;
}

esp_err_t crid_wifi_set_rid_ie(const uint8_t *ie, uint16_t len) {
    if (ie == NULL || len < 2 || ie[0] != 0xDD || len != (uint16_t)ie[1] + 2) {
        return ESP_ERR_INVALID_ARG;
    }

    // 同时挂到 Beacon 和 Probe Response，主动扫描也能拿到 RID 数据。
    // 注意: IDF 不允许直接覆盖已设置的 IE 槽位，必须先清除(enable=false)再设置。
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, ie);
    esp_err_t ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, ie);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set beacon vendor IE failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_0, ie);
    ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_0, ie);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set probe-resp vendor IE failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
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
