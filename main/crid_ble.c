/**
 * @file crid_ble.c
 * @brief 蓝牙数据通道（NimBLE 外设）：把网页需要的 REST 查询结果搬上蓝牙
 *
 * 数据流向：
 *   手机浏览器 --写 RX 特征(路径)--> 设备 --HTTP 访问 192.168.4.1--> 本机 httpd
 *   手机浏览器 <-通知 TX 特征(分片)-- 设备 <-- JSON--
 *
 * 走本机回环的好处是只维护一套接口实现：新增 REST 接口蓝牙自动可用。
 * lwIP 的 per-interface loopback（CONFIG_LWIP_NETIF_LOOPBACK，默认开）负责
 * 把发往本机 AP 地址的报文回灌给本机，不需要额外的网络。
 */

#include "crid_ble.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "CN_C-RID_BLE";

// ------------------------------------------------------------------
// 帧格式（与 crid_web.c 的 bleFetch 一一对应）
// ------------------------------------------------------------------
#define BLE_F_START 0x01
#define BLE_F_DATA  0x02
#define BLE_F_END   0x03
#define BLE_F_ERR   0x04

#define BLE_REQ_MAX 96      // 请求路径缓冲（够放 /api/log/points?limit=30&offset=270）

// Nordic UART 风格 UUID：通用蓝牙调试工具（nRF Connect 等）可以直接连上看数据
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_tx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_own_addr_type;
static char     s_name[24] = "ESP32-CRID";

static QueueHandle_t s_req_q;      // 请求路径队列（元素 BLE_REQ_MAX）
static TaskHandle_t  s_worker;

// ------------------------------------------------------------------
// 工具
// ------------------------------------------------------------------

static void ble_advertise(void);

/** 本机 AP 地址（蓝牙桥接靠它访问自己的 httpd） */
static void ap_ip_str(char *out, size_t n) {
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (ap != NULL && esp_netif_get_ip_info(ap, &ip) == ESP_OK && ip.ip.addr != 0) {
        snprintf(out, n, IPSTR, IP2STR(&ip.ip));
    } else {
        snprintf(out, n, "192.168.4.1");
    }
}

/** 发一帧（type + 数据）。分片大小按当前 MTU 走，MTU 不够时退回 20 字节 */
static int ble_send(uint8_t type, const uint8_t *data, int len) {
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) return -1;
    if (len < 0) len = 0;

    uint8_t buf[260];
    int mtu = ble_att_mtu(s_conn);
    int max_payload = (mtu > 3) ? (mtu - 3) : 20;
    if (max_payload > (int)sizeof(buf) - 1) max_payload = (int)sizeof(buf) - 1;
    if (len > max_payload) len = max_payload;

    buf[0] = type;
    if (len > 0) memcpy(buf + 1, data, len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, (uint16_t)(len + 1));
    if (om == NULL) return -1;

    int rc = ble_gatts_notify_custom(s_conn, s_tx_handle, om);
    if (rc != 0) {
        // 缓冲区不够时通知会失败：让出一点时间再试一次，仍失败就放弃这一帧
        vTaskDelay(pdMS_TO_TICKS(3));
        om = ble_hs_mbuf_from_flat(buf, (uint16_t)(len + 1));
        if (om == NULL) return -1;
        rc = ble_gatts_notify_custom(s_conn, s_tx_handle, om);
    }
    return rc == 0 ? len : -1;
}

static void ble_send_start(int total) {
    uint8_t b[4] = {
        (uint8_t)((total >> 24) & 0xFF), (uint8_t)((total >> 16) & 0xFF),
        (uint8_t)((total >> 8) & 0xFF),  (uint8_t)(total & 0xFF)
    };
    ble_send(BLE_F_START, b, 4);
}

/**
 * @brief 把本机 httpd 的响应读出来分片发给手机
 *
 * 只放行 /api/ 开头的 GET；响应体一律当二进制透传，网页侧再 JSON.parse。
 */
static void ble_proxy(const char *path) {
    if (path == NULL || path[0] != '/' || strncmp(path, "/api/", 5) != 0) {
        uint8_t e = 1;                       // 1 = 路径不合法
        ble_send(BLE_F_ERR, &e, 1);
        return;
    }

    char ip[16];
    ap_ip_str(ip, sizeof(ip));

    char url[160];
    snprintf(url, sizeof(url), "http://%s%s", ip, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .buffer_size = 512,
        .buffer_size_tx = 256,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        uint8_t e = 2;                       // 2 = 客户端创建失败
        ble_send(BLE_F_ERR, &e, 1);
        return;
    }

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "本机 HTTP 打开失败(%s): %s", path, esp_err_to_name(err));
        uint8_t e = 3;                       // 3 = 连接本机 httpd 失败
        ble_send(BLE_F_ERR, &e, 1);
        esp_http_client_cleanup(cli);
        return;
    }

    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG, "%s -> HTTP %d", path, status);
        uint8_t e = 4;                       // 4 = 接口返回非 200
        ble_send(BLE_F_ERR, &e, 1);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return;
    }

    ble_send_start(total > 0 ? total : 0);

    uint8_t buf[512];
    int sent = 0;
    int n;
    while ((n = esp_http_client_read(cli, (char *)buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < n) {
            int r = ble_send(BLE_F_DATA, buf + off, n - off);
            if (r <= 0) goto done;           // 对端断开或无内存，直接收工
            off += r;
            sent += r;
            vTaskDelay(pdMS_TO_TICKS(2));    // 给协议栈喘息，避免 ACL 缓冲打满
        }
    }

done:
    ble_send(BLE_F_END, NULL, 0);
    ESP_LOGI(TAG, "%s -> %d 字节", path, sent);

    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
}

/** 桥接任务：串行处理请求（一次只有一个手机，不需要并发） */
static void ble_worker(void *arg) {
    char path[BLE_REQ_MAX];
    for (;;) {
        if (xQueueReceive(s_req_q, path, portMAX_DELAY) == pdTRUE) {
            if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_proxy(path);
        }
    }
}

// ------------------------------------------------------------------
// GATT
// ------------------------------------------------------------------

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                      void *arg) {
    (void)attr;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char path[BLE_REQ_MAX];
        uint16_t len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, path, sizeof(path) - 1, &len);
        if (rc != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        path[len] = '\0';

        s_conn = conn;
        if (xQueueSend(s_req_q, path, 0) != pdTRUE) {
            // 队列满：丢掉最旧的一条再放，避免网页卡死在一个旧请求上
            char tmp[BLE_REQ_MAX];
            xQueueReceive(s_req_q, tmp, 0);
            xQueueSend(s_req_q, path, 0);
        }
        return 0;
    }

    // TX 特征只用于通知，读它没有意义
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // 手机写入请求路径
                .uuid = &s_rx_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // 设备分片回传响应
                .uuid = &s_tx_uuid.u,
                .access_cb = chr_access,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 }
};

// ------------------------------------------------------------------
// GAP
// ------------------------------------------------------------------

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "手机已连接蓝牙（conn=%d）", s_conn);
        } else {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "蓝牙已断开（reason=0x%x），继续广播", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU=%d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void ble_advertise(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "广播字段设置失败: %d", rc);
        return;
    }

    // 服务 UUID 放进扫描响应，网页 requestDevice 的 filters 才能按服务过滤
    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.uuids128 = &s_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params advp;
    memset(&advp, 0, sizeof(advp));
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &advp, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "广播启动失败: %d", rc);
    }
}

static void ble_on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "无可用蓝牙地址: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "地址类型推断失败: %d", rc);
        return;
    }
    ble_advertise();
    ESP_LOGI(TAG, "蓝牙数据通道就绪，广播名 %s", s_name);
}

static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "蓝牙协议栈复位: %d", reason);
}

static void ble_host_task(void *param) {
    (void)param;
    nimble_port_run();              // 直到 nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ------------------------------------------------------------------
// 对外接口
// ------------------------------------------------------------------

bool crid_ble_connected(void) {
    return s_conn != BLE_HS_CONN_HANDLE_NONE;
}

esp_err_t crid_ble_init(void) {
    // 广播名带 MAC 后两位，多台设备同时开机时手机列表里能分清
    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    snprintf(s_name, sizeof(s_name), "ESP32-CRID-%02X%02X", mac[4], mac[5]);

    s_req_q = xQueueCreate(4, BLE_REQ_MAX);
    if (s_req_q == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.store_status_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 服务注册失败: %d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_device_name_set(s_name);

    // 桥接任务：栈要够 http 客户端 + 512 字节缓冲
    if (xTaskCreate(ble_worker, "crid_ble", 5120, NULL, 4, &s_worker) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}
