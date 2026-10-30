#include "crid_messages.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"
#include "crid_time.h"

static const char *TAG = "CN_C-RID_MSG";

// ================================================================
// 精度档位（随时间缓慢轮换）
// ================================================================
// 真实飞控的定位精度随卫星几何（HDOP/VDOP）、多径环境变化，几分钟到几十分钟
// 一个量级地漂，但绝不会每帧乱跳。旧实现把「水平 <3m / 垂直 <10m / 速度 <0.3m/s」
// 写死成常数，等于对外声明「我的传感器永远不会变差」，是明显的程序特征。
// 这里按 60 秒窗口掷一次骰子，窗口内保持稳定。
static uint32_t acc_slot(void) {
    uint64_t ms = crid_time_epoch_ms();
    if (ms == 0) ms = (uint64_t)(esp_timer_get_time() / 1000);   // 未校时用单调时钟
    return (uint32_t)(ms / 60000ULL);
}

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// 水平精度：12 = <1m（多数情况），11 = <3m（偶发）
uint8_t crid_acc_horiz(void) {
    return (hash32(acc_slot() * 31u + 1u) % 100u < 85u) ? 12u : 11u;
}
// 垂直精度：6 = <1m，4 = <10m
uint8_t crid_acc_vert(void) {
    return (hash32(acc_slot() * 37u + 2u) % 100u < 85u) ? 6u : 4u;
}
// 速度精度：4 = <0.3m/s，3 = <1m/s
uint8_t crid_acc_speed(void) {
    return (hash32(acc_slot() * 41u + 3u) % 100u < 90u) ? 4u : 3u;
}
// 气压精度：4 = <10m，2 = <45m（0 = Unknown，旧实现一直报 Unknown 却给了有效值）
uint8_t crid_acc_baro(void) {
    return (hash32(acc_slot() * 43u + 4u) % 100u < 70u) ? 4u : 2u;
}

// --- 打包消息常量 ---
// 计算打包消息长度: 头部3字节 + 5条报文 * 25字节 = 128
#define PACKED_MSG_HEADER_LEN 3
#define PACKED_MSG_COUNT      5
#define PACKED_MSG_TOTAL_LEN (PACKED_MSG_HEADER_LEN + PACKED_MSG_COUNT * CRID_MESSAGE_SIZE)

// --- 辅助函数：写入 int32_t 为小端序 ---
static inline void write_le32(uint8_t *buf, int32_t val) {
    for (int i = 0; i < 4; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

// --- 辅助函数：写入 uint16_t 为小端序 ---
static inline void write_le16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

// --- 辅助函数：写入 uint32_t 为小端序 ---
static inline void write_le32_u32(uint8_t *buf, uint32_t val) {
    for (int i = 0; i < 4; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

// --- 编码地速 (GB 42590-2023 表 A.9 / ASTM F3411-22a) ---
// 速度 < 63.75 m/s（SpeedMult=0）: encoded = speed / 0.25，取值 0~254
// 速度 >= 63.75 m/s（SpeedMult=1）: encoded = (speed - 63.75) / 0.75，取值 0~254
// 解码：mult=0 -> code*0.25；mult=1 -> code*0.75 + 63.75
// 速度 > 254.25 m/s: encoded = 254 (上限饱和)
static uint8_t encode_ground_speed(float speed_ms) {
    if (speed_ms < 0.0f) speed_ms = 0.0f;
    if (speed_ms < 63.75f) {
        return (uint8_t)(speed_ms / 0.25f + 0.5f);
    } else if (speed_ms <= 254.25f) {
        return (uint8_t)((speed_ms - 63.75f) / 0.75f + 0.5f);
    } else {
        return 254;
    }
}

// --- 编码高度 (0.5m 精度，偏移 -1000m) ---
// 符合 ASTM F3411: encoded = (altitude_m + 1000) / 0.5
// 有效范围: -1000m ~ 31767.5m
// 特殊值: 0 = Invalid/Unknown (-1000m)
static uint16_t encode_altitude(float altitude_m) {
    if (altitude_m < -1000.0f) altitude_m = -1000.0f;
    int32_t val = (int32_t)((altitude_m + 1000.0f) / 0.5f);
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    return (uint16_t)val;
}

void crid_build_basic_id_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [消息类型(高4位)] + [接口版本(低4位)]
    message[0] = (MSG_TYPE_BASIC_ID << 4) | 0x01;

    // 字节1: [UAType(4)][IDType(4)] — OpenDroneID 位序
    message[1] = (config->ua_type & 0x0F) | ((config->id_type & 0x0F) << 4);

    // 字节2-21: UAS ID (20字节, ASCII, 不足填充 NULL)
    memset(&message[2], 0x00, CRID_UAS_ID_MAX_LEN);
    size_t id_len = strlen(config->uas_id);
    if (id_len > CRID_UAS_ID_MAX_LEN) id_len = CRID_UAS_ID_MAX_LEN;
    memcpy(&message[2], config->uas_id, id_len);

    // 字节22-24: 预留
    // 已由 memset 置零

    ESP_LOGD(TAG, "Basic ID message built (UAS: %s)", config->uas_id);
}

void crid_build_location_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [消息类型(高4位)] + [接口版本(低4位)]
    message[0] = (MSG_TYPE_LOCATION << 4) | 0x01;

    // 字节1: [Status(4)][Reserved(1)][HeightType(1)][EWDirection(1)][SpeedMult(1)]
    // SpeedMult: 0 = speed < 255*0.25=63.75, 1 = speed >= 63.75
    // EWDirection: 0 = 航向 0-179°, 1 = 航向 180-359°（字节2 存减 180 后的值）
    uint8_t speed_mult = (config->speed_horizontal >= 63.75f) ? 1 : 0;
    uint8_t height_type = config->height_type & 0x01;
    uint8_t ew_dir;
    uint8_t track_angle;

    // 字节2: 航迹角（0-179，配合 EWDirection 表示 0-359°）
    // 无效值：GB 42590 表 A.9 规定航迹角报到 361°，即 E/W=1 且编码值 181
    if (config->heading < 0.0f || config->heading >= 360.0f) {
        ew_dir = 1;
        track_angle = 181;
    } else if (config->heading >= 180.0f) {
        ew_dir = 1;
        track_angle = (uint8_t)(config->heading - 180.0f + 0.5f);
        if (track_angle > 179) track_angle = 179;
    } else {
        ew_dir = 0;
        track_angle = (uint8_t)(config->heading + 0.5f);
        if (track_angle > 179) track_angle = 179;
    }
    // OpenDroneID 位序: [Status(4)][Reserved(1)][HeightType(1)][EWDirection(1)][SpeedMult(1)]
    message[1] = (config->status << 4) | (height_type << 2) | (ew_dir << 1) | speed_mult;
    message[2] = track_angle;

    // 字节3: 地速
    message[3] = encode_ground_speed(config->speed_horizontal);

    // 字节4: 垂直速度（GB 42590-2023 表 A.9：8 位有符号数，0.5 m/s 精度，无偏移）
    // 编码: code = speed_vertical / 0.5 = speed_vertical * 2
    // 取值 -124 ~ +124 对应 -62 ~ +62 m/s；饱和到 ±124，避开 0xFF 无效码
    int16_t vs_enc = (int16_t)lroundf(config->speed_vertical * 2.0f);
    if (vs_enc > 124) vs_enc = 124;
    if (vs_enc < -124) vs_enc = -124;
    message[4] = (uint8_t)(int8_t)vs_enc;

    // 字节5-8: 纬度 (小端序, 1E-7 度单位)
    write_le32(&message[5], (int32_t)(config->latitude * 1e7));

    // 字节9-12: 经度 (小端序, 1E-7 度单位)
    write_le32(&message[9], (int32_t)(config->longitude * 1e7));

    // 字节13-14: 气压高度 (小端序, 0.5m 精度, 偏移 -1000m)
    // 独立参数「气压高度」+ 巡游带来的高度偏移 + 气压计自身的缓慢漂移。
    // 漂移项让「气压高度 - 几何高度」随时间缓慢变化（真实气压计受气温/气流
    // 影响就是这样游走的）；若两者差值恒为 0，等于宣称装了一块完美气压计。
    {
        float alt_off = config->altitude_msl - config->base_altitude_msl;
        write_le16(&message[13], encode_altitude(config->baro_altitude + alt_off + config->baro_drift));
    }

    // 字节15-16: 几何高度 (小端序, 0.5m 精度, 偏移 -1000m)
    write_le16(&message[15], encode_altitude(config->altitude_msl));

    // 字节17-18: 距地高度 (小端序, 0.5m 精度, 偏移 -1000m)
    // HeightType bit: 0 = over takeoff, 1 = over ground
    write_le16(&message[17], encode_altitude(config->altitude_agl));

    // 字节19: [VertAccuracy(4)][HorizAccuracy(4)]，档位随时间缓慢轮换
    message[19] = (uint8_t)((crid_acc_vert() << 4) | crid_acc_horiz());

    // 字节20: [BaroAccuracy(4)][SpeedAccuracy(4)]
    message[20] = (uint8_t)((crid_acc_baro() << 4) | crid_acc_speed());

    // 字节21-22: 时间戳 (自当前小时起的 0.1 秒单位，小端序)
    // 范围: 0 ~ 35999 (表示 0.0s ~ 3599.9s)；无效值: 0xFFFF
    //
    // 关键：绝不能拿「1970-01-01 + 开机秒数」当年内时间用。设备没有 RTC，
    // 未授时时 gettimeofday 给的是 1970 年，算出来的「小时内秒数」虽然落在
    // 合法区间，却和真实时钟完全无关；System 报文那边更会直接溢出成 2057 年。
    // 统一由 crid_time 提供时间：未校准就老老实实写「无效」。
    {
        uint64_t epoch = crid_time_epoch_ms();
        if (epoch == 0) {
            write_le16(&message[21], 0xFFFF);
        } else {
            uint32_t sec = (uint32_t)(epoch / 1000u);
            uint32_t ts  = (sec % 3600u) * 10u + (uint32_t)((epoch % 1000u) / 100u);
            if (ts > 35999u) ts = 35999u;
            write_le16(&message[21], (uint16_t)ts);
        }
    }

    // 字节23: [Reserved2(4)][TSAccuracy(4)]
    message[23] = (0x00 << 4) | 0x02; // TSAccuracy = 0.2s

    // 字节24: 预留 (已由 memset 置零)

    ESP_LOGD(TAG, "Location message built (%.6f, %.6f)", config->latitude, config->longitude);
}

void crid_build_system_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头
    message[0] = (MSG_TYPE_SYSTEM << 4) | 0x01;

    // 字节1: [Reserved(3)][ClassificationType(3)][OperatorLocationType(2)]
    message[1] = (config->classification_type << 2) | (config->operator_location_type & 0x03);

    // 字节2-5: 控制站纬度 (小端序, 1E-7)
    write_le32(&message[2], (int32_t)(config->operator_lat * 1e7));

    // 字节6-9: 控制站经度 (小端序, 1E-7)
    write_le32(&message[6], (int32_t)(config->operator_lon * 1e7));

    // 字节10-11: 运行区域计数
    write_le16(&message[10], 1);

    // 字节12: 运行区域半径（1 LSB = 10 m）
    // 随盘旋半径自适应：高速时盘旋圈会变大，若还写死 100 m，无人机会绕到
    // 自己声明的运行区域之外，是很容易被规则检查抓到的矛盾。
    {
        float area_r = config->patrol_radius_m * 2.0f + 50.0f;
        if (area_r < 100.0f)  area_r = 100.0f;
        if (area_r > 2550.0f) area_r = 2550.0f;
        message[12] = (uint8_t)(area_r / 10.0f + 0.5f);
    }

    // 字节13-14: 运行区域高度上限 (0.5m 精度, 偏移 -1000m)
    // 字节15-16: 运行区域高度下限
    // 以当前高度为中心各放宽 50 m，避免「区域写 50~100 m、实际飞到 150 m」的矛盾
    {
        float alt_hi = config->altitude_msl + 50.0f;
        float alt_lo = config->altitude_msl - 50.0f;
        if (alt_lo < 0.0f) alt_lo = 0.0f;
        write_le16(&message[13], encode_altitude(alt_hi));
        write_le16(&message[15], encode_altitude(alt_lo));
    }

    // 字节17: [UA运行类别(7-4位)][UA等级(3-0位)] —— GB 42590 表 A.7
    message[17] = ((config->category_eu & 0x0F) << 4) | (config->class_eu & 0x0F);

    // 字节18-19: 操作员高度 (0.5m 精度, 偏移 -1000m)
    write_le16(&message[18], encode_altitude(config->operator_alt));

    // 字节20-23: 时间戳 (自 2019-01-01 00:00:00 UTC 的秒数，小端序)
    // 0 = 未知。设备无 RTC，未授时时 system time 是 1970-01-01 + uptime，
    // 直接相减会得到负数，强转 uint32 后变成 2057 年——这是最容易被一眼识破
    // 的伪造特征，所以这里必须走统一时间基准并做下界保护。
    {
        uint64_t epoch = crid_time_epoch_ms();
        uint32_t ts_since_2019 = 0;
        if (epoch != 0) {
            int64_t s = (int64_t)(epoch / 1000u) - 1546300800LL;
            ts_since_2019 = (s > 0) ? (uint32_t)s : 0;
        }
        write_le32_u32(&message[20], ts_since_2019);
    }

    // 字节24: 预留

    ESP_LOGD(TAG, "System message built (Op: %.6f, %.6f, Alt: %.1fm, ID: %s)",
             config->operator_lat, config->operator_lon,
             config->operator_alt, config->operator_id);
}

void crid_build_self_desc_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)] — 小端位序
    message[0] = 0x01 | (MSG_TYPE_SELF_DESC << 4);

    // 字节1: DescType
    message[1] = DESC_TYPE_TEXT;

    // 字节2-24: 描述字符串 (最多 23 字节 ASCII, 不足填充 NULL)
    memset(&message[2], 0x00, 23);
    size_t id_len = strlen(config->drone_name);
    if (id_len > 23) id_len = 23;
    memcpy(&message[2], config->drone_name, id_len);

    ESP_LOGD(TAG, "Self-Description message built (Drone: %s)", config->drone_name);
}

void crid_build_auth_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)]
    message[0] = 0x01 | (MSG_TYPE_AUTH << 4);
    (void)config; // unused when auth is none

    // 字节1: [AuthType(4)][DataPage(4)]
    message[1] = (0x00 << 4) | 0x00; // AuthType=None, Page=0

    // 字节2: LastPageIndex
    message[2] = 0;

    // 字节3: Length
    message[3] = 0;

    // 字节4-7: Timestamp (relative to 2019-01-01)，未授时写 0（未知）
    {
        uint64_t epoch = crid_time_epoch_ms();
        uint32_t ts_since_2019 = 0;
        if (epoch != 0) {
            int64_t s = (int64_t)(epoch / 1000u) - 1546300800LL;
            ts_since_2019 = (s > 0) ? (uint32_t)s : 0;
        }
        write_le32_u32(&message[4], ts_since_2019);
    }

    // 字节8-24: AuthData (17 bytes for page 0)
    // 全部置零表示无认证数据

    ESP_LOGD(TAG, "Authentication message built (None)");
}

void crid_build_operator_id_message(const cn_crid_config_t *config, uint8_t *message) {
    memset(message, 0, CRID_MESSAGE_SIZE);

    // 报头: [ProtoVersion(4)][MessageType(4)]
    message[0] = 0x01 | (MSG_TYPE_OPERATOR_ID << 4);

    // 字节1: OperatorIdType
    message[1] = 0x00; // CAA Registration ID

    // 字节2-21: OperatorId (20 bytes, NULL padded)
    memset(&message[2], 0x00, 20);
    size_t id_len = strlen(config->operator_id);
    if (id_len > 20) id_len = 20;
    memcpy(&message[2], config->operator_id, id_len);

    // 字节22-24: Reserved

    ESP_LOGD(TAG, "Operator ID message built (%s)", config->operator_id);
}

// --- 构建打包消息（5 条报文，不含认证报文，符合 GB42590 / IB-TM-2024-01） ---
// 输出 128 字节: [0xF1][25][5] + 5×25 字节报文
static void crid_pack_messages(const cn_crid_config_t *config, uint8_t *packed_msg) {
    uint8_t packed_pos = 0;

    // 打包格式标识
    packed_msg[packed_pos++] = 0xF1;
    // 每条消息长度: 25
    packed_msg[packed_pos++] = CRID_MESSAGE_SIZE;
    // 消息数量: 5
    packed_msg[packed_pos++] = PACKED_MSG_COUNT;

    // 1. Basic ID 报文
    uint8_t basic_msg[CRID_MESSAGE_SIZE];
    crid_build_basic_id_message(config, basic_msg);
    memcpy(&packed_msg[packed_pos], basic_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 2. Location 报文
    uint8_t location_msg[CRID_MESSAGE_SIZE];
    crid_build_location_message(config, location_msg);
    memcpy(&packed_msg[packed_pos], location_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 3. Self-ID 报文
    uint8_t self_desc_msg[CRID_MESSAGE_SIZE];
    crid_build_self_desc_message(config, self_desc_msg);
    memcpy(&packed_msg[packed_pos], self_desc_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 4. System 报文
    uint8_t system_msg[CRID_MESSAGE_SIZE];
    crid_build_system_message(config, system_msg);
    memcpy(&packed_msg[packed_pos], system_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;

    // 5. Operator ID 报文
    uint8_t operator_id_msg[CRID_MESSAGE_SIZE];
    crid_build_operator_id_message(config, operator_id_msg);
    memcpy(&packed_msg[packed_pos], operator_id_msg, CRID_MESSAGE_SIZE);
    packed_pos += CRID_MESSAGE_SIZE;
}

// --- 构建 RID 厂商 IE（用于 esp_wifi_set_vendor_ie 挂到 softAP Beacon/Probe Response） ---
// 完整 IE: [0xDD][len=133][FA 0B BC][0x0D][counter][packed 128 字节]，共 135 字节
uint16_t crid_build_vendor_ie(cn_crid_config_t *config,
                              uint8_t *out, uint16_t max_len) {
    if (config == NULL || out == NULL) return 0;
    const uint16_t ie_len = 4 + 1 + PACKED_MSG_TOTAL_LEN;   // OUI(3)+Type(1)+Counter(1)+Packed(128) = 133
    const uint16_t total = ie_len + 2;                       // 含 ElementID 和 Length 字节 = 135
    if (max_len < total) {
        ESP_LOGE(TAG, "Vendor IE buffer too small: %u < %u", max_len, total);
        return 0;
    }

    uint16_t pos = 0;
    out[pos++] = 0xDD;              // Vendor Specific IE ID
    out[pos++] = (uint8_t)ie_len;   // Length

    // OUI: FA 0B BC
    out[pos++] = CRID_OUI_0;
    out[pos++] = CRID_OUI_1;
    out[pos++] = CRID_OUI_2;

    // Vendor Type: 0x0D
    out[pos++] = CRID_VENDOR_TYPE;

    // Message Counter（每构建一次 +1，255 后回绕到 0）
    out[pos++] = config->message_counter;
    config->message_counter++;

    // 打包消息
    crid_pack_messages(config, &out[pos]);
    pos += PACKED_MSG_TOTAL_LEN;

    return pos;
}

bool crid_build_beacon_frame(cn_crid_config_t *config,
                              uint8_t *frame, uint16_t max_len,
                              uint16_t *out_len) {
    if (config == NULL || frame == NULL || out_len == NULL) return false;

    uint16_t pos = 0;

    // --- 预估帧长度，确保不越界 ---
    // MAC Header: 24 + Timestamp: 8 + Beacon Interval: 2 + Capability: 2 + SSID IE: 2+ssid_len
    // + Rates IE: 2+8 + DS IE: 3 + Vendor IE: 1+1+3+1+1+3+25*5 = ~216
    #define BEACON_FRAME_ESTIMATED_LEN 280
    if (max_len < BEACON_FRAME_ESTIMATED_LEN) {
        ESP_LOGE(TAG, "Frame buffer too small: %u < %u", max_len, BEACON_FRAME_ESTIMATED_LEN);
        return false;
    }

    // --- MAC Header (24 bytes) ---
    frame[pos++] = 0x80; // Type=Management, Subtype=Beacon
    frame[pos++] = 0x00;
    frame[pos++] = 0x00; // Duration
    frame[pos++] = 0x00;

    // Destination Address (Broadcast)
    memset(&frame[pos], 0xFF, 6);
    pos += 6;

    // Source Address
    memcpy(&frame[pos], config->mac_address, 6);
    pos += 6;

    // BSSID
    memcpy(&frame[pos], config->mac_address, 6);
    pos += 6;

    // Sequence Control
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    // --- Beacon Body ---
    // Timestamp (8 bytes)
    memset(&frame[pos], 0, 8);
    pos += 8;

    // Beacon Interval: 100 ms ≈ 98 TU（802.11 时间单元 1.024 ms）
    write_le16(&frame[pos], 98);
    pos += 2;

    // Capability Information
    frame[pos++] = 0x21;
    frame[pos++] = 0x04;

    // --- SSID IE ---
    frame[pos++] = 0x00; // IE ID
    size_t ssid_len = strlen(config->ssid);
    frame[pos++] = (uint8_t)ssid_len;
    memcpy(&frame[pos], config->ssid, ssid_len);
    pos += ssid_len;

    // --- Supported Rates IE ---
    frame[pos++] = 0x01; // IE ID
    frame[pos++] = 0x08; // Length
    uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(&frame[pos], rates, 8);
    pos += 8;

    // --- DS Parameter Set IE ---
    frame[pos++] = 0x03;
    frame[pos++] = 0x01;
    frame[pos++] = config->channel;

    // --- China C-RID Vendor Specific IE ---
    // 复用厂商 IE 构建函数（[0xDD][len][OUI][Type][Counter][Packed]）
    uint8_t msg_counter = config->message_counter;
    uint16_t ie_written = crid_build_vendor_ie(config, &frame[pos], max_len - pos);
    if (ie_written == 0) {
        return false;
    }
    pos += ie_written;

    *out_len = pos;

    ESP_LOGI(TAG, "Beacon frame built: %u bytes, counter=%u, pos=(%.6f,%.6f)",
             *out_len, msg_counter, config->latitude, config->longitude);
    return true;
}
