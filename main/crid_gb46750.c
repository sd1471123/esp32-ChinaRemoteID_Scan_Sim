/**
 * @file crid_gb46750.c
 * @brief GB 46750-2025 新国标运行识别报文构建器实现
 *
 * 字节布局与接收端 main_rx/crid_parser.c 的 decode_gb46750_payload() 严格对齐：
 * 数据内容按标识位图 bit7->bit1 顺序排列（字节1 -> 字节2 -> 字节3）。
 *
 * 编码规则（GB 46750-2025 表3）:
 *   001 唯一产品识别码  20B ASCII（左对齐，空位填 NULL）
 *   002 实名登记标志     8B ASCII（未登记填 NULL）
 *   004 无人机分类       1B（0=微型 1=轻型 2=小型 3=中型 4=大型）
 *   005 遥控站位置类型   1B（0=起飞点 1=遥控站）
 *   006 遥控站位置       8B 小端「经度|纬度」int32 x1e-7，未知 FFFFFFFF
 *   007 遥控站高度       2B 小端 (值+1000)x2，0.5m 分辨率，0=未知
 *   008 无人机位置       8B 同 006
 *   009 航迹角           2B 小端 uint16 x0.1°，向下取整，0xFFFF=无效
 *   010 地速             2B 小端 uint16 x0.1m/s，向下取整，0xFFFF=无效
 *   011 相对高度         2B 小端 (值+9000)x2，0=未知
 *   012 垂直速度         1B（bit8=符号，低7位 |值|x2，0xFF=未知）
 *   013 大地高度         2B 小端 (值+1000)x2，0=未知
 *   014 气压高度         2B 小端 (值+1000)x2，0=未知（可选）
 *   015 运行状态         1B（0=未报告 1=地面 2=空中 3=紧急 4/5=识别失效）
 *   016 坐标系类型       1B（0=WGS-84）
 *   017 水平精度         1B（11=<3m 12=<1m）
 *   018 垂直精度         1B（4=<10m 6=<1m）
 *   019 速度精度         1B（2=<3m/s 3=<1m/s 4=<0.3m/s）
 *   020 时间戳           6B 小端 Unix 毫秒
 *   021 时间戳精度       1B（4=<=0.2s）
 */

#include "crid_gb46750.h"
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "sys/time.h"
#include "esp_log.h"

static const char *TAG = "CN_C-RID_GB46750";

// --- 时间校准偏移（浏览器授时，单位毫秒；0 = 未校准） ---
static int64_t s_epoch_offset_ms = 0;

static inline uint64_t uptime_ms(void) {
    return (uint64_t)esp_log_timestamp();   // 开机以来的毫秒数
}

void crid_gb46750_set_epoch_ms(uint64_t epoch_ms) {
    if (epoch_ms == 0) {
        s_epoch_offset_ms = 0;
        return;
    }
    s_epoch_offset_ms = (int64_t)epoch_ms - (int64_t)uptime_ms();
    ESP_LOGI(TAG, "系统时间已校准: %llu ms（偏移 %lld ms）",
             (unsigned long long)epoch_ms, (long long)s_epoch_offset_ms);
}

/** 当前 Unix 毫秒；未校准时返回 0（标准中 020 的“未知”取值） */
static inline uint64_t current_epoch_ms(void) {
    if (s_epoch_offset_ms == 0) return 0;
    int64_t v = (int64_t)uptime_ms() + s_epoch_offset_ms;
    return (v > 0) ? (uint64_t)v : 0;
}

// --- 辅助：小端写 int32 ---
static inline void w_le32(uint8_t *buf, int32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

// --- 辅助：小端写 uint16 ---
static inline void w_le16(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

// --- 高度编码: (m + 1000) * 2，0 = 未知（表3 第007/013/014 项） ---
static inline uint16_t enc_alt(float alt_m) {
    if (!(alt_m > -1000.0f)) return 0;          // 未知/无效 → 0
    float v = (alt_m + 1000.0f) * 2.0f + 0.5f;
    if (v < 1.0f)     v = 1.0f;
    if (v > 65535.0f) v = 65535.0f;
    return (uint16_t)v;
}

// --- 相对高度编码: (m + 9000) * 2，0 = 未知（表3 第011 项，偏移与大地高度不同） ---
static inline uint16_t enc_rel_alt(float agl_m) {
    if (!(agl_m > -9000.0f)) return 0;
    float v = (agl_m + 9000.0f) * 2.0f + 0.5f;
    if (v < 1.0f)     v = 1.0f;
    if (v > 65535.0f) v = 65535.0f;
    return (uint16_t)v;
}

// --- 位置编码: 32 位经度 | 32 位纬度，小端，未知取 FFFFFFFF（表3 第006/008 项） ---
static inline void w_pos(uint8_t *p, float lon, float lat) {
    bool ok = (lon >= -180.0f && lon <= 180.0f &&
               lat >= -90.0f  && lat <= 90.0f) &&
              !(lon == 0.0f && lat == 0.0f);   // (0,0) 视为未定位
    if (ok) {
        w_le32(p,     (int32_t)((double)lon * 1e7 + (lon >= 0 ? 0.5 : -0.5)));
        w_le32(p + 4, (int32_t)((double)lat * 1e7 + (lat >= 0 ? 0.5 : -0.5)));
    } else {
        w_le32(p,     -1);
        w_le32(p + 4, -1);
    }
}

uint16_t crid_build_gb46750_ie(cn_crid_config_t *config,
                               uint8_t *out, uint16_t max_len) {
    if (config == NULL || out == NULL) return 0;

    // ---- 数据内容区（按标识位图 bit7->bit1 顺序填充） ----
    uint8_t content[128];
    uint16_t clen = 0;

    // ===== 标识字节1 =====
    // 001 唯一产品识别码 (20B ASCII，左对齐，空位填 NULL)
    {
        char id[20] = {0};
        size_t n = strlen(config->uas_id);
        if (n > 20) n = 20;
        memcpy(id, config->uas_id, n);
        memcpy(&content[clen], id, 20);
        clen += 20;
    }
    // 002 实名登记标志 (8B ASCII，未登记填 NULL)
    {
        char rn[8] = {0};
        size_t n = strlen(config->realname_id);
        if (n > 8) n = 8;
        memcpy(rn, config->realname_id, n);
        memcpy(&content[clen], rn, 8);
        clen += 8;
    }
    // 004 无人机分类 (1B)
    content[clen++] = config->ua_category_gb & 0x0F;
    // 005 遥控站位置类型 (1B)
    content[clen++] = config->operator_location_type & 0x0F;
    // 006 遥控站位置 (8B 小端 经度|纬度 x1e-7)
    w_pos(&content[clen], config->operator_lon, config->operator_lat);
    clen += 8;
    // 007 遥控站高度 (2B)
    w_le16(&content[clen], enc_alt(config->operator_alt));
    clen += 2;

    // ===== 标识字节2 =====
    // 008 无人机位置 (8B 小端 经度|纬度)
    w_pos(&content[clen], config->longitude, config->latitude);
    clen += 8;
    // 009 航迹角 (2B x0.1°，向下取整)
    if (config->heading < 0.0f || config->heading >= 360.0f) {
        w_le16(&content[clen], 0xFFFF);   // 无效
    } else {
        uint16_t trk = (uint16_t)(config->heading * 10.0f);   // 向下取整
        if (trk > 3599) trk = 3599;
        w_le16(&content[clen], trk);
    }
    clen += 2;
    // 010 地速 (2B x0.1m/s，向下取整)
    if (config->speed_horizontal < 0.0f) {
        w_le16(&content[clen], 0xFFFF);
    } else {
        uint32_t spd = (uint32_t)(config->speed_horizontal * 10.0f);   // 向下取整
        if (spd > 65534) spd = 65534;
        w_le16(&content[clen], (uint16_t)spd);
    }
    clen += 2;
    // 011 相对高度 (2B，偏移 9000)
    w_le16(&content[clen], enc_rel_alt(config->altitude_agl));
    clen += 2;
    // 012 垂直速度 (1B：bit8=符号位，0=上升 1=下降；低 7 位为 |v|x2，0xFF=无效)
    {
        float vs = config->speed_vertical;
        if (!(vs > -63.5f && vs < 63.5f)) {
            content[clen] = 0xFF;                 // 未知/超范围
        } else {
            float mag = (vs >= 0.0f ? vs : -vs) * 2.0f + 0.5f;
            uint8_t code = (uint8_t)mag;
            if (code > 0x7F) code = 0x7F;
            if (vs < 0.0f) code |= 0x80;
            content[clen] = code;
        }
        clen += 1;
    }
    // 013 大地高度 (2B)
    w_le16(&content[clen], enc_alt(config->altitude_msl));
    clen += 2;
    // 014 气压高度 (2B，编码同 013)
    // 气压计以起飞点为基准，自身不随几何高度变化；这里按「用户声明的气压高度 +
    // 巡游带来的高度偏移」输出，保证高度波动时两者的差值（气压/几何差）保持不变
    {
        float alt_off = config->altitude_msl - config->base_altitude_msl;
        w_le16(&content[clen], enc_alt(config->baro_altitude + alt_off));
        clen += 2;
    }

    // ===== 标识字节3 =====
    // 015 运行状态 (1B)
    content[clen++] = config->status & 0x0F;
    // 016 坐标系类型 (1B) — 0 = WGS-84
    content[clen++] = 0x00;
    // 017 水平精度 (1B) — 11 = <3m
    content[clen++] = 11;
    // 018 垂直精度 (1B) — 4 = <10m
    content[clen++] = 4;
    // 019 速度精度 (1B) — 2 = <3m/s
    content[clen++] = 2;
    // 020 时间戳 (6B 小端 Unix 毫秒；未校时则写 0 = 未知)
    {
        uint64_t ms = current_epoch_ms();
        for (int i = 0; i < 6; i++) {
            content[clen + i] = (uint8_t)((ms >> (i * 8)) & 0xFF);
        }
        clen += 6;
    }
    // 021 时间戳精度 (1B) — 4 = 小于或等于 0.2s
    content[clen++] = 4;

    // ---- 组装完整 IE ----
    // [0xDD][len][OUI(3)=FA0BBC][0xF0][0xFF 数据类型][ver][dlen][flags(3)][content]
    // 包头共 12 字节（不含 content），len 字节 = total - 2
    const uint16_t total = 12 + clen;
    if (max_len < total) {
        ESP_LOGE(TAG, "buffer too small: %u < %u", max_len, total);
        return 0;
    }

    uint16_t pos = 0;
    out[pos++] = 0xDD;                            // Vendor Specific IE
    out[pos++] = (uint8_t)(total - 2);            // Length（不含 ID/Length）
    out[pos++] = CRID_OUI_0;
    out[pos++] = CRID_OUI_1;
    out[pos++] = CRID_OUI_2;
    out[pos++] = CRID_VENDOR_TYPE_GB46750;        // IE 类型 0xF0（新国标）
    out[pos++] = 0xFF;                            // 数据类型，固定 255
    out[pos++] = GB46750_VERSION_BYTE;            // 版本号 V1.0（高 3 位 001）
    out[pos++] = (uint8_t)clen;                   // 数据长度（仅内容项字节数）
    out[pos++] = GB46750_FLAGS_BYTE0;             // 数据标识字节1（含扩展位 0x01）
    out[pos++] = GB46750_FLAGS_BYTE1;             // 数据标识字节2（含扩展位 0x01）
    out[pos++] = GB46750_FLAGS_BYTE2;             // 数据标识字节3（扩展位 0 = 结束）
    memcpy(&out[pos], content, clen);
    pos += clen;

    config->message_counter++;   // 仅用于网页/日志显示，不占用报文空间
    return pos;
}
