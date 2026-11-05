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
#include "crid_web.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
#define BLE_REQ_BODY_MAX 1536   // POST 请求体上限（配置 JSON 约 700~900 字节）

// ---- 手机 → 设备 方向的控制字节 ----
// 老格式（网页在用）：整包就是一条 /api/... 路径，首字节必然是 '/'(0x2F)。
// 新格式（App 写配置用）：首字节 0x12/0x13，与 '/' 天然区分，老客户端不受影响。
#define BLE_RX_POST_HDR 0x12    // 0x12 | pathlen(1) | bodylen(2, 小端) | path | body 开头
#define BLE_RX_POST_DAT 0x13    // 0x13 | body 续片

typedef struct {
    char     path[BLE_REQ_MAX];
    uint16_t method;        // 0 = GET, 1 = POST
    uint16_t body_len;
    char    *body;          // POST 时堆分配，worker 用完 free
} ble_req_t;

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

static QueueHandle_t s_req_q;      // 请求队列（元素 ble_req_t）
static TaskHandle_t  s_worker;

// 手机发来的 POST 请求体要分几帧才能写完（MTU 247 时每帧 244 字节），
// 这里做一次简单重组。同时只有一个客户端、一次只有一条请求，用静态缓冲最省事。
static struct {
    bool     active;
    char     path[BLE_REQ_MAX];
    uint16_t body_len;
    uint16_t got;
    char     body[BLE_REQ_BODY_MAX];
} s_rx;

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
 * GET 走明文 80 端口（那里只放行 GET /api/ 前缀，延迟最低，轮询每秒一次靠它）；
 * POST 走 HTTPS 443——80 端口按设计只放行 GET，写操作走加密口既不用破坏那条
 * 策略，也不会把配置（含热点密码）明文过一遍。设备连自己，自签证书不校验。
 *
 * 响应体一律当二进制透传，App 侧再 JSON 解析。
 */
static void ble_proxy(const ble_req_t *r) {
    const char *path = r->path;
    if (path[0] != '/' || strncmp(path, "/api/", 5) != 0) {
        uint8_t e = 1;                       // 1 = 路径不合法
        ble_send(BLE_F_ERR, &e, 1);
        return;
    }

    bool post = (r->method == 1);
    char ip[16];
    ap_ip_str(ip, sizeof(ip));

    char url[176];
    snprintf(url, sizeof(url), "%s://%s%s", post ? "https" : "http", ip, path);

    ESP_LOGI(TAG, "%s %s body=%u", post ? "POST" : "GET", path, (unsigned)r->body_len);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        // POST 要等 TLS 握手 + 写 flash，给宽一点
        .timeout_ms = post ? 8000 : 5000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
        // 自签证书链上不可能有可信 CA，直接把本机这把证书当信任锚传下去。
        // 注意：只设 skip_cert_common_name_check 是没用的——IDF 的 esp-tls 在
        // 「既没有 cacert 也没有 crt_bundle」时会直接返回
        // ESP_ERR_MBEDTLS_SSL_SETUP_FAILED（No server verification option set），
        // 根本走不到 CN 校验那一步。
        .cert_pem = post ? crid_web_cert_pem() : NULL,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        uint8_t e = 2;                       // 2 = 客户端创建失败
        ble_send(BLE_F_ERR, &e, 1);
        return;
    }

    if (post) esp_http_client_set_header(cli, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(cli, post ? r->body_len : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "本机 HTTP 打开失败(%s): %s", path, esp_err_to_name(err));
        uint8_t e = 3;                       // 3 = 连接本机 httpd 失败
        ble_send(BLE_F_ERR, &e, 1);
        esp_http_client_cleanup(cli);
        return;
    }

    if (post && r->body_len > 0) {
        int w = esp_http_client_write(cli, r->body, r->body_len);
        if (w != (int)r->body_len) {
            ESP_LOGW(TAG, "%s 请求体写入不完整 (%d/%u)", path, w, (unsigned)r->body_len);
        }
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

/** 把一条请求放进队列（队列满时丢最旧的一条，避免手机卡在一个旧请求上） */
static void ble_enqueue(uint16_t method, const char *path, const char *body, uint16_t body_len) {
    ble_req_t r;
    memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.method = method;
    r.body_len = body_len;

    if (method == 1 && body_len > 0) {
        r.body = malloc(body_len);
        if (r.body == NULL) {
            uint8_t e = 5;                   // 5 = 设备内存不足
            ble_send(BLE_F_ERR, &e, 1);
            return;
        }
        memcpy(r.body, body, body_len);
    }

    if (xQueueSend(s_req_q, &r, 0) != pdTRUE) {
        ble_req_t drop;
        if (xQueueReceive(s_req_q, &drop, 0) == pdTRUE) free(drop.body);
        xQueueSend(s_req_q, &r, 0);
    }
}

/** 桥接任务：串行处理请求（一次只有一个手机，不需要并发） */
static void ble_worker(void *arg) {
    ble_req_t r;
    for (;;) {
        if (xQueueReceive(s_req_q, &r, portMAX_DELAY) == pdTRUE) {
            if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_proxy(&r);
            free(r.body);
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
        // 一帧最多 MTU-3 字节（MTU 247 时 244），留点余量
        uint8_t buf[260];
        uint16_t len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
        if (rc != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (len == 0) return 0;

        s_conn = conn;

        // ---- POST 头帧：0x12|pathlen|bodylen(2,小端)|path|body 开头 ----
        if (buf[0] == BLE_RX_POST_HDR) {
            if (len < 4) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            uint8_t plen = buf[1];
            uint16_t blen = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
            if (plen == 0 || plen >= sizeof(s_rx.path) || blen > BLE_REQ_BODY_MAX) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            if (len < 4 + plen) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

            memcpy(s_rx.path, buf + 4, plen);
            s_rx.path[plen] = '\0';
            s_rx.body_len = blen;
            s_rx.got = 0;
            s_rx.active = true;

            int off = 4 + plen;
            int avail = len - off;
            if (avail > (int)blen) avail = (int)blen;
            if (avail > 0) {
                memcpy(s_rx.body, buf + off, avail);
                s_rx.got = (uint16_t)avail;
            }
            if (s_rx.got >= s_rx.body_len) {
                s_rx.active = false;
                ESP_LOGI(TAG, "收到 POST %s（首帧即收齐，%u 字节）",
                         s_rx.path, (unsigned)s_rx.body_len);
                ble_enqueue(1, s_rx.path, s_rx.body, s_rx.body_len);
            }
            return 0;
        }

        // ---- POST 续片：0x13|data... ----
        if (buf[0] == BLE_RX_POST_DAT) {
            if (!s_rx.active) {
                ESP_LOGW(TAG, "收到续帧但没有进行中的 POST（%u 字节）", (unsigned)len);
                return BLE_ATT_ERR_UNLIKELY;
            }
            int avail = len - 1;
            int room = (int)s_rx.body_len - (int)s_rx.got;
            if (avail > room) avail = room;
            if (avail > 0) {
                memcpy(s_rx.body + s_rx.got, buf + 1, avail);
                s_rx.got = (uint16_t)(s_rx.got + avail);
            }
            if (s_rx.got >= s_rx.body_len) {
                s_rx.active = false;
                ESP_LOGI(TAG, "收到 POST %s（%u 字节）", s_rx.path, (unsigned)s_rx.body_len);
                ble_enqueue(1, s_rx.path, s_rx.body, s_rx.body_len);
            }
            return 0;
        }

        // ---- 老格式：整包就是一条 GET 路径（网页在用，必须原样支持）----
        char path[BLE_REQ_MAX];
        uint16_t n = len < sizeof(path) - 1 ? len : sizeof(path) - 1;
        memcpy(path, buf, n);
        path[n] = '\0';
        ble_enqueue(0, path, NULL, 0);
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
    // 广播间隔放宽到 400~500 ms（默认 30~60 ms 一秒要发二十来包）。
    // Wi-Fi AP 与 BLE 共用天线，广播越密，AP 的 Beacon 与数据帧被抢占得越厉害，
    // 手机连上热点却加载不出页面。这里降到每秒两包，扫得到的概率几乎不变。
    advp.itvl_min = BLE_GAP_ADV_ITVL_MS(400);
    advp.itvl_max = BLE_GAP_ADV_ITVL_MS(500);

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

    s_req_q = xQueueCreate(4, sizeof(ble_req_t));
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

    // 桥接任务：GET 只需要 http 客户端 + 1KB 缓冲；POST 走 TLS，握手要几 KB 栈，
    // 7168 会在首次握手时栈溢出，这里给到 12288。
    if (xTaskCreate(ble_worker, "crid_ble", 12288, NULL, 4, &s_worker) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}
