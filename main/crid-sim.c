/**
 * @file crid-sim.c
 * @brief ESP32 中国民用无人机远程识别 (C-RID) 模拟器 - 主入口
 *
 * 双模式（网页可切换，NVS 持久化）:
 *   - 发射模式: softAP vendor IE 广播 RID（GB 42590-2023 旧国标 /
 *     GB 46750-2025 新国标）
 *   - 接收模式: 混杂模式监听周边无人机 RID 广播并解析（网页展示）
 *
 * softAP 始终运行（网页配置入口 http://192.168.4.1 不受模式影响）。
 */

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "crid_config.h"
#include "crid_messages.h"
#include "crid_gb46750.h"
#include "crid_wifi.h"
#include "crid_web.h"
#include "crid_patrol.h"
#include "crid_rx.h"

static const char *TAG = "ESP32_CRID_STD";

// --- 全局 RID 厂商 IE 缓冲区 ---
#define RID_IE_BUF_SIZE 160
static uint8_t g_rid_ie_42590[RID_IE_BUF_SIZE];   // 槽位1: GB42590 打包报文
static uint8_t g_rid_ie_46750[RID_IE_BUF_SIZE];   // 槽位0: GB46750 位图报文
static bool    s_gb_ie_logged = false;            // 报文头是否已打印（每次切换标准重置）

// --- 全局配置 ---
static cn_crid_config_t g_config;

// --- 发射端当前生效的标准快照（检测网页切换，触发 IE 重建） ---
static uint8_t s_applied_standard = 0xFF;

// --- 位置更新间隔 ---
#define PATROL_INTERVAL_SEC 10

/**
 * @brief 按当前标准把 IE 挂到对应槽位
 *
 * 标准切换时先清空两个槽位再重挂，避免残留旧标准 IE。
 * @return true = 发射生效（任一 IE 挂载成功）
 */
static bool apply_tx_ie(void) {
    uint8_t std = g_config.rid_standard;

    if (std != s_applied_standard) {
        // 标准变化: 清空双槽位，下一轮重挂
        crid_wifi_clear_rid_ie(0);
        crid_wifi_clear_rid_ie(1);
        s_applied_standard = std;
        s_gb_ie_logged = false;
        ESP_LOGI(TAG, "广播标准切换: %s",
                 std == RID_STD_GB42590 ? "GB42590 旧国标" : "GB46750 新国标");
    }

    bool ok = false;
    if (std == RID_STD_GB42590) {
        uint16_t len = crid_build_vendor_ie(&g_config, g_rid_ie_42590, RID_IE_BUF_SIZE);
        if (len > 0 && crid_wifi_set_rid_ie(1, g_rid_ie_42590, len) == ESP_OK) ok = true;
    }
    if (std == RID_STD_GB46750) {
        uint16_t len = crid_build_gb46750_ie(&g_config, g_rid_ie_46750, RID_IE_BUF_SIZE);
        if (len > 0 && crid_wifi_set_rid_ie(0, g_rid_ie_46750, len) == ESP_OK) {
            ok = true;
            // 首次挂载时打印完整报文，便于与 GB 46750-2025 表1/表2/表3 逐字节核对
            if (!s_gb_ie_logged) {
                ESP_LOGI(TAG, "GB46750 IE %uB: [DD][len][FA 0B BC][F0][FF][ver][dlen][flags x3][content]",
                         len);
                ESP_LOG_BUFFER_HEX(TAG, g_rid_ie_46750, len);
                s_gb_ie_logged = true;
            }
        }
    }
    return ok;
}

/**
 * @brief 应用当前工作模式（网页切换 / 启动时调用）
 *
 * 发射模式: 关闭混杂模式，发送任务自动恢复 IE 广播
 * 接收模式: 发送任务自动摘除 IE，开启混杂模式监听
 */
void crid_app_apply_mode(void) {
    if (g_config.device_mode == DEVICE_MODE_RX) {
        crid_rx_start(g_config.channel);
    } else {
        crid_rx_stop();
        s_applied_standard = 0xFF;   // 强制下一轮重建 IE
    }
}

/**
 * @brief C-RID 主任务（发射/接收按配置分发）
 */
static void crid_main_task(void *pvParameter) {
    ESP_LOGI(TAG, "C-RID main task started (mode=%s)",
             g_config.device_mode == DEVICE_MODE_RX ? "接收" : "发射");

    TickType_t xLastWake10s = xTaskGetTickCount();
    const TickType_t xInterval10s = pdMS_TO_TICKS(PATROL_INTERVAL_SEC * 1000);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(g_config.beacon_interval_ms));

        if (g_config.device_mode == DEVICE_MODE_RX) {
            // 接收模式: 确保 IE 已摘除（首帧进入时清一次）
            if (s_applied_standard != 0xFF) {
                crid_wifi_clear_rid_ie(0);
                crid_wifi_clear_rid_ie(1);
                s_applied_standard = 0xFF;
                ESP_LOGI(TAG, "发射 IE 已摘除，进入接收模式");
            }
        } else {
            // 发射模式: 定时重建并更新厂商 IE（softAP 硬件负责广播）
            if (!apply_tx_ie()) {
                ESP_LOGE(TAG, "Failed to set RID vendor IE");
            }
        }

        // --- 10 秒定时：更新巡游位置（仅发射模式有意义） ---
        TickType_t xNow = xTaskGetTickCount();
        if ((xNow - xLastWake10s) >= xInterval10s) {
            if (g_config.device_mode == DEVICE_MODE_TX) {
                ESP_LOGI(TAG, "=== Patrol position update ===");
                crid_patrol_step(&g_config);
            }
            xLastWake10s = xNow;
        }
    }
}

/**
 * @brief 应用主入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 China C-RID Simulator ===");
    ESP_LOGI(TAG, "Standards: GB42590-2023 / GB46750-2025");

    // 0. 日志降噪：以下两类是协议栈的常规噪音，不反映任何故障
    //    - httpd_txrx: 手机端关闭页面/切后台/网络切换会主动 RST 连接（errno 104 ECONNRESET），
    //      ESP-IDF httpd 只是优雅关闭该 socket 并继续服务，属于正常路径；
    //    - wifi: AP 模式下的 Block ACK 维护（ADDBA RX DELBA），属 WiFi 协议栈例行日志。
    //    只关这两个 TAG，CN_C-RID_* 业务日志与其它组件日志全部保留。
    esp_log_level_set("httpd_txrx", ESP_LOG_NONE);
    esp_log_level_set("wifi", ESP_LOG_WARN);

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

    // 4. 初始化 C-RID 配置（默认值 + NVS 已保存的用户配置）
    crid_config_init_default(&g_config);
    crid_config_load_nvs(&g_config);

    // 5. 初始化 Wi-Fi（softAP 模式始终运行：网页配置入口）
    ret = crid_wifi_init_ap(g_config.channel, g_config.ssid, g_config.beacon_interval_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return;
    }

    // 6. 应用初始工作模式（接收模式则开启混杂监听）
    crid_app_apply_mode();

    // 7. 启动主任务
    BaseType_t task_ret = xTaskCreate(crid_main_task, "cn_crid_main",
                                      6144, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task!");
        return;
    }

    // 8. 启动网页配置服务器（手机连接热点后访问 http://192.168.4.1）
    if (crid_web_start(&g_config) != ESP_OK) {
        ESP_LOGE(TAG, "Web config server failed to start!");
    }

    ESP_LOGI(TAG, "C-RID started successfully! (mode=%s, std=%u)",
             g_config.device_mode == DEVICE_MODE_RX ? "RX" : "TX",
             g_config.rid_standard);
    ESP_LOGI(TAG, "  UAS ID (Model): %s", g_config.uas_id);
    ESP_LOGI(TAG, "  Drone Name: %s", g_config.drone_name);
    ESP_LOGI(TAG, "  Operator ID: %s", g_config.operator_id);
    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_config.mac_address[0], g_config.mac_address[1], g_config.mac_address[2],
             g_config.mac_address[3], g_config.mac_address[4], g_config.mac_address[5]);
    ESP_LOGI(TAG, "  Position: %.6f, %.6f", g_config.latitude, g_config.longitude);
    ESP_LOGI(TAG, "  Channel: %u, Interval: %ums", g_config.channel, g_config.beacon_interval_ms);
    ESP_LOGI(TAG, "  Web config: http://192.168.4.1");

    // 临时校验用：构建一份 GB46750 报文并打印（不改变当前广播标准）
    {
        static uint8_t dbg[160];
        uint16_t n = crid_build_gb46750_ie(&g_config, dbg, sizeof(dbg));
        if (n) {
            ESP_LOGI(TAG, "GB46750 IE preview %uB", n);
            ESP_LOG_BUFFER_HEX(TAG, dbg, n);
        }
    }
}
