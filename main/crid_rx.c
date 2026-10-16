/**
 * @file crid_rx.c
 * @brief 接收模式模块实现
 *
 * 数据流: 混杂模式回调(WiFi 任务上下文，仅拷贝入队) -> 队列 -> 解析任务
 *         -> 更新航迹表 -> 网页 /api/rx 轮询快照
 *
 * 解析策略（与 main_rx/crid_parser.c 一致）:
 *   1. GB 46750-2025: [counter][0xFF][ver][clen][flags3][content]
 *   2. GB 42590/ASTM 打包: [counter][0xF1][size25][count][messages]
 *      交给 opendroneid 库 odid_message_process_pack() 解码
 */

#include "crid_rx.h"
#include "crid_config.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "opendroneid.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "CN_C-RID_RX";

/* ================================================================
 * 内部类型
 * ================================================================ */

// 队列消息（回调中拷贝的 RID vendor IE 数据）
typedef struct {
    uint8_t src_mac[6];
    int8_t  rssi;
    uint8_t channel;
    uint8_t data[192];      // vendor IE payload（OUI/Type 之后）
    uint8_t data_len;
} rx_msg_t;

// 内部航迹（含 opendroneid 解码状态）
typedef struct {
    bool     active;
    uint8_t  mac[6];
    uint32_t last_seen_ms;
    uint32_t first_seen_ms;
    int8_t   last_rssi;
    uint8_t  last_channel;
    uint8_t  protocol;
    uint32_t msg_count;
    ODID_UAS_Data odid;             // GB42590/ASTM 解码目标
    // GB 46750 解码结果
    struct {
        bool valid;
        char unique_id[21];
        char realname_id[9];
        uint8_t ua_category;
        double lat, lon;
        float geo_alt, track, gspeed, vspeed;
        uint8_t status;
        // 飞手(遥控站)位置：位图数据项 006/007；位图未携带时保留上次值(sticky)
        bool op_valid;
        double op_lat, op_lon;
        float op_geo_alt;
    } gb;
} rx_track_t;

/* ================================================================
 * 模块状态
 * ================================================================ */

#define RX_QUEUE_SIZE 16

static QueueHandle_t s_queue = NULL;
static TaskHandle_t  s_task = NULL;
static volatile bool s_running = false;

static rx_track_t s_tracks[CRID_RX_MAX_TRACKS];

static volatile uint32_t s_total_packets = 0;
static volatile uint32_t s_rid_detect = 0;
static volatile uint32_t s_parse_ok = 0;
static volatile uint32_t s_q_overflow = 0;

/* ================================================================
 * GB 46750-2025 解码（移植自 main_rx/crid_parser.c，字节布局一致）
 * ================================================================ */

static inline uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline int32_t le32s(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                   | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static inline float decode_alt_2byte(const uint8_t *p) {
    uint16_t raw = le16(p);
    if (raw == 0) return 0.0f;   // 未知
    return (raw / 2.0f) - 1000.0f;
}

/**
 * 按标识位图（3 字节，bit7->bit1 每字节顺序）解码数据内容到 gb 结构
 * @return 解析成功的数据项数量
 */
static int decode_gb46750_content(rx_track_t *t,
                                  const uint8_t *flags,
                                  const uint8_t *content, uint8_t content_len) {
    int offset = 0, items = 0;
    t->gb.valid = false;

    for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
        uint8_t flag = flags[byte_idx];
        for (int bit = 7; bit >= 1; bit--) {
            if (!(flag & (1 << bit))) continue;
            if (offset >= content_len) return items;

            if (byte_idx == 0) {
                switch (bit) {
                    case 7:   // 001 唯一产品识别码 20B
                        if (offset + 20 > content_len) return items;
                        memcpy(t->gb.unique_id, &content[offset], 20);
                        t->gb.unique_id[20] = '\0';
                        offset += 20;
                        break;
                    case 6:   // 002 实名登记标志 8B
                        if (offset + 8 > content_len) return items;
                        memcpy(t->gb.realname_id, &content[offset], 8);
                        t->gb.realname_id[8] = '\0';
                        offset += 8;
                        break;
                    case 4:   // 004 无人机分类 1B
                        t->gb.ua_category = content[offset];
                        offset += 1;
                        break;
                    case 3:   // 005 遥控站位置类型 1B（暂不存储）
                        offset += 1;
                        break;
                    case 2:   // 006 遥控站位置 8B（飞手经纬度）
                        if (offset + 8 > content_len) return items;
                        t->gb.op_lat = le32s(&content[offset]) / 1e7;
                        t->gb.op_lon = le32s(&content[offset + 4]) / 1e7;
                        if (!(t->gb.op_lat == 0 && t->gb.op_lon == 0)) {
                            t->gb.op_valid = true;   // (0,0) 表示未知，保持 sticky
                        }
                        offset += 8;
                        break;
                    case 1:   // 007 遥控站高度 2B
                        t->gb.op_geo_alt = decode_alt_2byte(&content[offset]);
                        offset += 2;
                        break;
                    default: return items;
                }
            } else if (byte_idx == 1) {
                switch (bit) {
                    case 7:   // 008 无人机位置 8B
                        if (offset + 8 > content_len) return items;
                        t->gb.lat = le32s(&content[offset]) / 1e7;
                        t->gb.lon = le32s(&content[offset + 4]) / 1e7;
                        offset += 8;
                        break;
                    case 6:   // 009 航迹角 2B x0.1°
                        if (offset + 2 > content_len) return items;
                        {
                            uint16_t raw = le16(&content[offset]);
                            t->gb.track = (raw != 0xFFFF) ? raw / 10.0f : 0.0f;
                        }
                        offset += 2;
                        break;
                    case 5:   // 010 地速 2B x0.1m/s
                        if (offset + 2 > content_len) return items;
                        {
                            uint16_t raw = le16(&content[offset]);
                            t->gb.gspeed = (raw != 0xFFFF) ? raw / 10.0f : 0.0f;
                        }
                        offset += 2;
                        break;
                    case 4: offset += 2; break;   // 011 相对高度（跳过）
                    case 3: offset += 1; break;   // 012 垂直速度（跳过）
                    case 2:   // 013 大地高度 2B
                        if (offset + 2 > content_len) return items;
                        t->gb.geo_alt = decode_alt_2byte(&content[offset]);
                        offset += 2;
                        break;
                    case 1: offset += 2; break;   // 014 气压高度（跳过）
                    default: return items;
                }
            } else {   // byte_idx == 2
                switch (bit) {
                    case 7:   // 015 运行状态 1B
                        t->gb.status = content[offset];
                        offset += 1;
                        break;
                    case 6: offset += 1; break;   // 016 坐标系（跳过）
                    case 5: offset += 1; break;   // 017 水平精度（跳过）
                    case 4: offset += 1; break;   // 018 垂直精度（跳过）
                    case 3: offset += 1; break;   // 019 速度精度（跳过）
                    case 2: offset += 6; break;   // 020 时间戳（跳过存储）
                    case 1: offset += 1; break;   // 021 时间戳精度（跳过）
                    default: return items;
                }
            }
            items++;
        }
    }

    t->gb.valid = true;
    return items;
}

/* ================================================================
 * GB 42590 / ASTM 打包报文解码（opendroneid 库）
 * ================================================================ */

static bool decode_packed(rx_track_t *t, const uint8_t *data, uint8_t len) {
    // data = [counter][0xF1][SingleMsgSize=25][MsgCount][messages...]
    if (len < 4) return false;
    uint8_t msg_size = data[2];
    uint8_t msg_count = data[3];
    if (msg_size != ODID_MESSAGE_SIZE || msg_count < 1 || msg_count > ODID_PACK_MAX_MESSAGES) {
        return false;
    }
    uint8_t body = len - 4;
    if (body < (uint8_t)(msg_count * ODID_MESSAGE_SIZE)) return false;

    // 构造 ODID_MessagePack_encoded 头部交给库解码
    uint8_t tmp[3 + ODID_PACK_MAX_MESSAGES * ODID_MESSAGE_SIZE];
    tmp[0] = (ODID_MESSAGETYPE_PACKED << 4) | 0x01;   // 0xF1
    tmp[1] = ODID_MESSAGE_SIZE;
    tmp[2] = msg_count;
    memcpy(&tmp[3], &data[4], msg_count * ODID_MESSAGE_SIZE);

    int ret = odid_message_process_pack(&t->odid, tmp,
                                        3 + msg_count * ODID_MESSAGE_SIZE);
    return ret > 0;
}

/* ================================================================
 * 解析任务：出队 -> 定位航迹 -> 解码 -> 更新
 * ================================================================ */

static rx_track_t *find_or_create(const uint8_t *mac) {
    rx_track_t *free_slot = NULL;
    for (int i = 0; i < CRID_RX_MAX_TRACKS; i++) {
        if (s_tracks[i].active) {
            if (memcmp(s_tracks[i].mac, mac, 6) == 0) return &s_tracks[i];
        } else if (!free_slot) {
            free_slot = &s_tracks[i];
        }
    }
    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        memcpy(free_slot->mac, mac, 6);
        free_slot->active = true;
        free_slot->first_seen_ms = esp_log_timestamp();
        odid_initUasData(&free_slot->odid);
    }
    return free_slot;
}

static void rx_parse_task(void *pv) {
    rx_msg_t msg;
    while (s_running) {
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }

        rx_track_t *t = find_or_create(msg.src_mac);
        if (t == NULL) continue;   // 航迹表已满

        t->last_seen_ms = esp_log_timestamp();
        t->last_rssi = msg.rssi;
        t->last_channel = msg.channel;
        t->msg_count++;

        bool ok = false;
        // 策略 1: GB 46750-2025
        if (msg.data_len >= 7 && msg.data[1] == 0xFF) {
            uint8_t ver = msg.data[2];
            uint8_t major = (ver >> 5) & 0x07;
            uint8_t clen = msg.data[3];
            if (major == 0x1 && clen <= (msg.data_len - 7)) {
                int items = decode_gb46750_content(t, &msg.data[4],
                                                   &msg.data[7], clen);
                if (items > 0) {
                    t->protocol = RX_PROTO_GB46750;
                    ok = true;
                }
            }
        }
        // 策略 2: GB 42590 / ASTM 打包
        if (!ok && msg.data_len >= 5 && msg.data[1] == 0xF1) {
            if (decode_packed(t, msg.data, msg.data_len)) {
                t->protocol = RX_PROTO_GB42590;
                ok = true;
            }
        }

        if (ok) s_parse_ok++;
    }
    vTaskDelete(NULL);
}

/* ================================================================
 * 混杂模式回调（WiFi 任务上下文：只做过滤与拷贝，尽快返回）
 * ================================================================ */

static inline bool is_rid_oui(uint8_t o0, uint8_t o1, uint8_t o2) {
    return (o0 == CRID_OUI_0 && o1 == CRID_OUI_1 && o2 == CRID_OUI_2);
}

static void rx_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    s_total_packets++;
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    if (pkt->rx_ctrl.sig_len <= 24) return;

    const uint8_t *pl = pkt->payload;
    uint8_t frame_ctrl_l = pl[0];
    uint8_t subtype = (frame_ctrl_l & 0xF0) >> 4;
    if ((frame_ctrl_l & 0x0C) != 0) return;              // 仅管理帧
    if (subtype != 8 && subtype != 5) return;            // 仅 Beacon / Probe Response

    // 遍历信息元素（24 字节 MAC 头之后）
    uint16_t frame_len = pkt->rx_ctrl.sig_len;
    uint16_t off = 24;
    while (off + 2 <= frame_len) {
        uint8_t ie_id = pl[off];
        uint8_t ie_len = pl[off + 1];
        if (off + 2 + (uint16_t)ie_len > frame_len) break;

        if (ie_id == 221 && ie_len >= 8) {               // Vendor Specific
            const uint8_t *v = &pl[off + 2];
            if (is_rid_oui(v[0], v[1], v[2]) && v[3] == CRID_VENDOR_TYPE) {
                s_rid_detect++;
                rx_msg_t m;
                memset(&m, 0, sizeof(m));
                memcpy(m.src_mac, &pl[10], 6);           // addr2 = 发送方 MAC
                m.rssi = pkt->rx_ctrl.rssi;
                m.channel = pkt->rx_ctrl.channel;
                uint8_t payload = ie_len - 4;            // 去掉 OUI(3)+Type(1)
                if (payload > sizeof(m.data)) payload = sizeof(m.data);
                memcpy(m.data, &v[4], payload);
                m.data_len = payload;
                if (xQueueSend(s_queue, &m, 0) != pdTRUE) s_q_overflow++;
            }
        }
        off += 2 + ie_len;
    }
}

/* ================================================================
 * 公开接口
 * ================================================================ */

esp_err_t crid_rx_start(uint8_t listen_channel) {
    if (s_running) return ESP_OK;

    s_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(rx_msg_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;

    esp_wifi_set_promiscuous_rx_cb(rx_sniffer_cb);
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);

    esp_err_t ret = esp_wifi_set_channel(listen_channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set channel %u failed: %s (AP 模式下跟随热点信道)",
                 listen_channel, esp_err_to_name(ret));
    }

    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "promiscuous failed: %s", esp_err_to_name(ret));
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ret;
    }

    s_running = true;
    memset(s_tracks, 0, sizeof(s_tracks));

    if (xTaskCreate(rx_parse_task, "crid_rx", 6144, NULL, 4, &s_task) != pdPASS) {
        s_running = false;
        esp_wifi_set_promiscuous(false);
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "接收模式启动: 监听信道 %u", listen_channel);
    return ESP_OK;
}

void crid_rx_stop(void) {
    if (!s_running) return;
    s_running = false;
    esp_wifi_set_promiscuous(false);

    // 等解析任务自行退出（最多 1s）
    for (int i = 0; i < 20 && s_task != NULL; i++) {
        if (eTaskGetState(s_task) == eDeleted || eTaskGetState(s_task) == eInvalid) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_task = NULL;

    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    memset(s_tracks, 0, sizeof(s_tracks));
    ESP_LOGI(TAG, "接收模式已停止");
}

int crid_rx_get_tracks(crid_rx_track_t *out) {
    if (out == NULL) return 0;
    int n = 0;
    uint32_t now = esp_log_timestamp();
    for (int i = 0; i < CRID_RX_MAX_TRACKS; i++) {
        rx_track_t *t = &s_tracks[i];
        if (!t->active) continue;
        if (now - t->last_seen_ms > 300000) {   // 5 分钟未更新视为离线
            t->active = false;
            continue;
        }
        crid_rx_track_t *o = &out[n++];
        memset(o, 0, sizeof(*o));
        o->active = true;
        memcpy(o->mac, t->mac, 6);
        o->rssi = t->last_rssi;
        o->channel = t->last_channel;
        o->protocol = t->protocol;
        o->msg_count = t->msg_count;
        o->last_seen_ms = t->last_seen_ms;
        o->first_seen_ms = t->first_seen_ms;

        if (t->protocol == RX_PROTO_GB46750 && t->gb.valid) {
            strncpy(o->uas_id, t->gb.unique_id, sizeof(o->uas_id) - 1);
            strncpy(o->realname_id, t->gb.realname_id, sizeof(o->realname_id) - 1);
            o->ua_type = t->gb.ua_category;
            o->latitude = t->gb.lat;
            o->longitude = t->gb.lon;
            o->altitude_geo = t->gb.geo_alt;
            o->speed_horizontal = t->gb.gspeed;
            o->direction = t->gb.track;
            o->status = t->gb.status;
            if (t->gb.op_valid) {
                o->has_op_loc = true;
                o->op_latitude = t->gb.op_lat;
                o->op_longitude = t->gb.op_lon;
                o->op_altitude_geo = t->gb.op_geo_alt;
            }
        } else {
            // GB42590 / ASTM: 从 opendroneid 数据提取
            if (t->odid.BasicIDValid[0]) {
                strncpy(o->uas_id, t->odid.BasicID[0].UASID, sizeof(o->uas_id) - 1);
                o->ua_type = (uint8_t)t->odid.BasicID[0].UAType;
            }
            if (t->odid.SelfIDValid) {
                strncpy(o->drone_name, t->odid.SelfID.Desc, sizeof(o->drone_name) - 1);
            }
            if (t->odid.OperatorIDValid) {
                strncpy(o->operator_id, t->odid.OperatorID.OperatorId,
                        sizeof(o->operator_id) - 1);
            }
            if (t->odid.LocationValid) {
                o->latitude = t->odid.Location.Latitude;
                o->longitude = t->odid.Location.Longitude;
                o->altitude_geo = t->odid.Location.AltitudeGeo;
                o->speed_horizontal = t->odid.Location.SpeedHorizontal;
                o->speed_vertical = t->odid.Location.SpeedVertical;
                o->direction = t->odid.Location.Direction;
                o->status = (uint8_t)t->odid.Location.Status;
            }
            // 飞手位置在 System 报文中（广播可选，多数消费机不携带）
            // 注：System 报文无独立遥控站高度字段，op_altitude_geo 置 0
            if (t->odid.SystemValid &&
                !(t->odid.System.OperatorLatitude == 0 &&
                  t->odid.System.OperatorLongitude == 0)) {
                o->has_op_loc = true;
                o->op_latitude = t->odid.System.OperatorLatitude;
                o->op_longitude = t->odid.System.OperatorLongitude;
            }
        }
    }
    return n;
}

void crid_rx_get_stats(uint32_t *total_packets, uint32_t *rid_detections,
                       uint32_t *parse_ok, uint32_t *queue_overflows) {
    if (total_packets)  *total_packets  = s_total_packets;
    if (rid_detections) *rid_detections = s_rid_detect;
    if (parse_ok)       *parse_ok       = s_parse_ok;
    if (queue_overflows)*queue_overflows= s_q_overflow;
}
