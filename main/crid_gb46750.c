/**
 * @file crid_gb46750.c
 * @brief GB 46750-2025 新国标运行识别报文构建器实现
 *
 * 字节布局与接收端 main_rx/crid_parser.c 的 decode_gb46750_payload() 严格对齐：
 * 数据内容按标识位图 bit7->bit1 顺序排列（字节1 -> 字节2 -> 字节3）。
 *
 * 编码规则（表3）:
 *   001 唯一产品识别码  20B ASCII（无则高位填 NULL，此处直接取 uas_id）
 *   002 实名登记标志     8B ASCII（未登记填 NULL/0x00）
 *   004 无人机分类       1B（0=微型 1=轻型 2=小型 3=中型 4=大型）
 *   005 遥控站位置类型   1B（0=起飞点 1=实时 GNSS 2=固定）
 *   006 遥控站位置       8B 小端 int32 x1e-7 度（纬度|经度）
 *   007 遥控站高度       2B 小端 (值+1000)x2，0.5m 分辨率，0=未知
 *   008 无人机位置       8B 同 006
 *   009 航迹角           2B 小端 uint16 x0.1°（0-3599，0xFFFF=无效）
 *   010 地速             2B 小端 uint16 x0.1m/s（0xFFFF=无效）
 *   013 大地高度         2B 小端 (值+1000)x2，0=未知
 *   015 运行状态         1B（0=未报告 1=地面 2=空中 3=紧急 4/5=识别失效）
 *   016 坐标系类型       1B（0=WGS84）
 *   017 水平精度         1B NACp 风格（11=<3m 12=<1m）
 *   018 垂直精度         1B GVA（4=<10m 6=<1m）
 *   019 速度精度         1B NACv（2=<3m/s 3=<1m/s 4=<0.3m/s）
 *   020 时间戳           6B 小端 Unix 毫秒
 *   021 时间戳精度       1B（1=<=0.2s）
 */

#include "crid_gb46750.h"
#include <string.h>
#include <time.h>
#include "sys/time.h"
#include "esp_log.h"

static const char *TAG = "CN_C-RID_GB46750";

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

// --- 高度编码: (m + 1000) * 2，0 = 未知 ---
static inline uint16_t enc_alt(float alt_m) {
    if (alt_m < -1000.0f) alt_m = -1000.0f;
    int32_t v = (int32_t)((alt_m + 1000.0f) * 2.0f + 0.5f);
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint16_t)v;
}

uint16_t crid_build_gb46750_ie(cn_crid_config_t *config,
                               uint8_t *out, uint16_t max_len) {
    if (config == NULL || out == NULL) return 0;

    // ---- 数据内容区（按标识位图 bit7->bit1 顺序填充） ----
    uint8_t content[128];
    uint16_t clen = 0;

    // ===== 标识字节1 =====
    // 001 唯一产品识别码 (20B ASCII)
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
    content[clen++] = config->operator_location_type & 0x03;
    // 006 遥控站位置 (8B 小端 lat|lon x1e-7)
    w_le32(&content[clen], (int32_t)(config->operator_lat * 1e7f));
    w_le32(&content[clen + 4], (int32_t)(config->operator_lon * 1e7f));
    clen += 8;
    // 007 遥控站高度 (2B)
    w_le16(&content[clen], enc_alt(config->operator_alt));
    clen += 2;

    // ===== 标识字节2 =====
    // 008 无人机位置 (8B)
    w_le32(&content[clen], (int32_t)(config->latitude * 1e7f));
    w_le32(&content[clen + 4], (int32_t)(config->longitude * 1e7f));
    clen += 8;
    // 009 航迹角 (2B x0.1°)
    if (config->heading < 0.0f || config->heading >= 360.0f) {
        w_le16(&content[clen], 0xFFFF);   // 无效
    } else {
        uint16_t trk = (uint16_t)(config->heading * 10.0f + 0.5f);
        if (trk > 3599) trk = 3599;
        w_le16(&content[clen], trk);
    }
    clen += 2;
    // 010 地速 (2B x0.1m/s)
    if (config->speed_horizontal < 0.0f || config->speed_horizontal > 6553.5f) {
        w_le16(&content[clen], 0xFFFF);
    } else {
        w_le16(&content[clen], (uint16_t)(config->speed_horizontal * 10.0f + 0.5f));
    }
    clen += 2;
    // 013 大地高度 (2B)
    w_le16(&content[clen], enc_alt(config->altitude_msl));
    clen += 2;

    // ===== 标识字节3 =====
    // 015 运行状态 (1B) — GB46750 0-5 与现有 0-4 枚举前段一致
    content[clen++] = config->status & 0x07;
    // 016 坐标系类型 (1B) — 0 = WGS84
    content[clen++] = 0x00;
    // 017 水平精度 (1B) — 11 = <3m
    content[clen++] = 11;
    // 018 垂直精度 (1B) — 4 = <10m
    content[clen++] = 4;
    // 019 速度精度 (1B) — 2 = <3m/s
    content[clen++] = 2;
    // 020 时间戳 (6B 小端 Unix 毫秒)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
        for (int i = 0; i < 6; i++) {
            content[clen + i] = (uint8_t)((ms >> (i * 8)) & 0xFF);
        }
        clen += 6;
    }
    // 021 时间戳精度 (1B) — 1 = <=0.2s
    content[clen++] = 1;

    // ---- 组装完整 IE ----
    // [0xDD][len][OUI(3)][Type(1)][counter(1)][0xFF][ver][clen][flags(3)][content]
    // 包头共 13 字节（不含 content），len 字节 = total - 2
    const uint16_t total = 13 + clen;
    if (max_len < total) {
        ESP_LOGE(TAG, "buffer too small: %u < %u", max_len, total);
        return 0;
    }

    uint16_t pos = 0;
    out[pos++] = 0xDD;                    // Vendor Specific IE ID
    out[pos++] = (uint8_t)(total - 2);    // Length（不含 ID/Length 两字节）
    out[pos++] = CRID_OUI_0;
    out[pos++] = CRID_OUI_1;
    out[pos++] = CRID_OUI_2;
    out[pos++] = CRID_VENDOR_TYPE;
    out[pos++] = config->message_counter; // 消息计数器
    config->message_counter++;
    out[pos++] = 0xFF;                    // 数据类型 = 255
    out[pos++] = GB46750_VERSION_BYTE;    // 版本号 V1.0
    out[pos++] = (uint8_t)clen;           // 数据内容长度
    out[pos++] = GB46750_FLAGS_BYTE0;     // 数据标识字节1
    out[pos++] = GB46750_FLAGS_BYTE1;     // 数据标识字节2
    out[pos++] = GB46750_FLAGS_BYTE2;     // 数据标识字节3
    memcpy(&out[pos], content, clen);
    pos += clen;

    return pos;
}
