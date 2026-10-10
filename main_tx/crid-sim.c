/**
 * @file crid-sim.c
 * @brief ESP32 中国民用无人机远程识别 (C-RID) 模拟发射器 - 主入口
 *
 * 符合 GB42590-2023 和《民用微轻小型无人驾驶航空器运行识别最低性能要求（试行）》
 */

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "crid_config.h"
#include "crid_messages.h"
#include "crid_wifi.h"
#include "crid_patrol.h"

static const char *TAG = "ESP32_CRID_STD";

// --- 全局 RID 厂商 IE 缓冲区（仅在发送任务中使用） ---
#define RID_IE_BUF_SIZE 160
static uint8_t g_rid_ie[RID_IE_BUF_SIZE];

// --- 全局配置 ---
static cn_crid_config_t g_config;

// --- 位置更新间隔 ---
#define PATROL_INTERVAL_SEC 10

/**
 * @brief C-RID 广播任务（softAP 模式）
 *
 * 每 DEFAULT_BEACON_INTERVAL_MS 更新一次厂商 IE（softAP 硬件自动以相同周期
 * 广播 Beacon 并应答探测请求），每 PATROL_INTERVAL_SEC 秒更新一次巡游位置。
 */
static void crid_send_beacon_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting China C-RID beacon transmission (softAP vendor IE mode)...");

    TickType_t xLastWake = xTaskGetTickCount();
    TickType_t xLastWake10s = xLastWake;

    const TickType_t xIntervalIe = pdMS_TO_TICKS(DEFAULT_BEACON_INTERVAL_MS);
    const TickType_t xInterval10s = pdMS_TO_TICKS(PATROL_INTERVAL_SEC * 1000);

    for (;;) {
        // --- 定时：重建并更新 RID 厂商 IE（计数器 +1，softAP 硬件负责广播） ---
        vTaskDelayUntil(&xLastWake, xIntervalIe);

        uint16_t ie_len = crid_build_vendor_ie(&g_config, g_rid_ie, RID_IE_BUF_SIZE);
        if (ie_len > 0) {
            esp_err_t ret = crid_wifi_set_rid_ie(g_rid_ie, ie_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set RID vendor IE: %s", esp_err_to_name(ret));
            }
        }

        // --- 10 秒定时：更新巡游位置（下个周期随 IE 一起广播） ---
        TickType_t xNow = xTaskGetTickCount();
        if ((xNow - xLastWake10s) >= xInterval10s) {
            ESP_LOGI(TAG, "=== Patrol position update ===");
            crid_patrol_step(&g_config);
            xLastWake10s = xNow;
        }
    }
}

/**
 * @brief 应用主入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 China C-RID Transmitter ===");
    ESP_LOGI(TAG, "Standard: GB42590-2023");

    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化 TCP/IP 网络接口
    ESP_ERROR_CHECK(esp_netif_init());

    // 3. 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 4. 初始化 C-RID 配置
    crid_config_init_default(&g_config);

    // 5. 初始化 Wi-Fi（softAP 模式：真实 Beacon + 应答探测，稳定可见）
    ret = crid_wifi_init_ap(g_config.channel, g_config.ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return;
    }

    // 6. 构建初始 RID 厂商 IE 并挂到 Beacon / Probe Response
    uint16_t ie_len = crid_build_vendor_ie(&g_config, g_rid_ie, RID_IE_BUF_SIZE);
    if (ie_len == 0 || crid_wifi_set_rid_ie(g_rid_ie, ie_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial RID vendor IE!");
        return;
    }

    // 7. 启动发送任务
    BaseType_t task_ret = xTaskCreate(crid_send_beacon_task, "cn_crid_tx",
                                      4096, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task!");
        return;
    }

    ESP_LOGI(TAG, "Transmitter started successfully!");
    ESP_LOGI(TAG, "  UAS ID (Model): %s", g_config.uas_id);
    ESP_LOGI(TAG, "  Drone Name: %s", g_config.drone_name);
    ESP_LOGI(TAG, "  Operator ID: %s", g_config.operator_id);
    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_config.mac_address[0], g_config.mac_address[1], g_config.mac_address[2],
             g_config.mac_address[3], g_config.mac_address[4], g_config.mac_address[5]);
    ESP_LOGI(TAG, "  Position: %.6f, %.6f", g_config.latitude, g_config.longitude);
    ESP_LOGI(TAG, "  Channel: %u, Interval: %ums", g_config.channel, DEFAULT_BEACON_INTERVAL_MS);
    ESP_LOGI(TAG, "  OUI: FA:0B:BC, Vendor Type: 0x0D (GB42590-2023)");
}
