/**
 * @file crid_gb46750.h
 * @brief GB 46750-2025 新国标运行识别报文构建器（位图数据项格式）
 *
 * 报文载体：Wi-Fi Beacon / Probe Response 的厂商特定 IE（Element ID 221）
 *   [0xDD][len][OUI FA:0B:BC][0xF0][数据类型 0xFF][版本号][数据长度]
 *   [数据标识 3B][数据内容项...]
 *
 * 关键约定（GB 46750-2025 §5.2 表 1/表 2/表 3）：
 *   - IE 类型字节为 0xF0（区别于 ASTM/GB42590 的 0x0D），其后紧跟数据类型 0xFF，
 *     两者之间没有其它字节；
 *   - 版本号高 3 位固定 001（V1.x）；
 *   - 数据长度 = 数据内容项字节数（不含数据标识）；
 *   - 数据标识每字节 bit7..bit1 为内容标志位，bit0 为扩展标志位（1=下一字节继续）；
 *   - 006/008 位置字段为「32 位经度 | 32 位纬度」，小端，未知取 0xFFFFFFFF。
 *
 * 本实现发送 17 项必选（001/002/004/005/006/007/008/009/010/013/015/016/017/
 * 018/019/020/021）以及 011 相对高度、012 垂直速度、014 气压高度三项可选数据。
 */

#ifndef CRID_GB46750_H
#define CRID_GB46750_H

#include "crid_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- 数据标识位定义（GB 46750 表2，位序与接收端解析一致） ---
// 标识字节1
#define GB46750_BIT_UNIQUE_ID      0x80  // 001 唯一产品识别码 (M) 20B ASCII
#define GB46750_BIT_REALNAME_FLAG  0x40  // 002 实名登记标志 (M) 8B ASCII
#define GB46750_BIT_OP_CATEGORY    0x20  // 003 运行类别 (O) 1B
#define GB46750_BIT_UA_CATEGORY    0x10  // 004 无人机分类 (M) 1B
#define GB46750_BIT_RCS_LOC_TYPE   0x08  // 005 遥控站位置类型 (M) 1B
#define GB46750_BIT_RCS_LOCATION   0x04  // 006 遥控站位置 (M) 8B
#define GB46750_BIT_RCS_ALTITUDE   0x02  // 007 遥控站高度 (M) 2B
// 标识字节2
#define GB46750_BIT_UAV_LOCATION   0x80  // 008 无人机位置 (M) 8B
#define GB46750_BIT_TRACK_ANGLE    0x40  // 009 航迹角 (M) 2B x0.1°
#define GB46750_BIT_GROUND_SPEED   0x20  // 010 地速 (M) 2B x0.1m/s
#define GB46750_BIT_REL_HEIGHT     0x10  // 011 相对高度 (O) 2B
#define GB46750_BIT_VERT_SPEED     0x08  // 012 垂直速度 (O) 1B
#define GB46750_BIT_GEO_ALTITUDE   0x04  // 013 大地高度 (M) 2B
#define GB46750_BIT_BARO_ALTITUDE  0x02  // 014 气压高度 (O) 2B
// 标识字节3
#define GB46750_BIT_OP_STATUS      0x80  // 015 运行状态 (M) 1B
#define GB46750_BIT_COORD_SYSTEM   0x40  // 016 坐标系类型 (M) 1B
#define GB46750_BIT_H_ACCURACY     0x20  // 017 水平精度 (M) 1B
#define GB46750_BIT_V_ACCURACY     0x10  // 018 垂直精度 (M) 1B
#define GB46750_BIT_SPEED_ACCURACY 0x08  // 019 速度精度 (M) 1B
#define GB46750_BIT_TIMESTAMP      0x04  // 020 时间戳 (M) 6B Unix 毫秒
#define GB46750_BIT_TS_ACCURACY    0x02  // 021 时间戳精度 (M) 1B

// 必选项位图（13 项必选全部发送 + 011 相对高度 / 012 垂直速度两项可选）
// 注意：每个数据标识字节的 bit0（0x01）是「扩展标志位」——1 表示下一字节仍是
// 数据标识，0 表示数据标识字段结束（GB 46750-2025 表 1/表 2）。发送 3 字节
// 位图时，第 1、2 字节的扩展位必须置 1，否则接收端会把后两个字节当成数据内容。
#define GB46750_EXT_NEXT    0x01
#define GB46750_FLAGS_BYTE0 (GB46750_BIT_UNIQUE_ID | GB46750_BIT_REALNAME_FLAG | \
                             GB46750_BIT_UA_CATEGORY | GB46750_BIT_RCS_LOC_TYPE | \
                             GB46750_BIT_RCS_LOCATION | GB46750_BIT_RCS_ALTITUDE | \
                             GB46750_EXT_NEXT)
#define GB46750_FLAGS_BYTE1 (GB46750_BIT_UAV_LOCATION | GB46750_BIT_TRACK_ANGLE | \
                             GB46750_BIT_GROUND_SPEED | GB46750_BIT_REL_HEIGHT | \
                             GB46750_BIT_VERT_SPEED | GB46750_BIT_GEO_ALTITUDE | \
                             GB46750_BIT_BARO_ALTITUDE | GB46750_EXT_NEXT)
#define GB46750_FLAGS_BYTE2 (GB46750_BIT_OP_STATUS | GB46750_BIT_COORD_SYSTEM | \
                             GB46750_BIT_H_ACCURACY | GB46750_BIT_V_ACCURACY | \
                             GB46750_BIT_SPEED_ACCURACY | GB46750_BIT_TIMESTAMP | \
                             GB46750_BIT_TS_ACCURACY)

// 版本号 V1.0: 高3位=001(主版本1), 低5位=子版本0
#define GB46750_VERSION_BYTE 0x20

/**
 * @brief 构建 GB 46750-2025 完整厂商 IE
 *
 * IE 结构: [0xDD][len][FA 0B BC][0xF0][0xFF][ver][dlen][flags3][content...]
 *
 * @param config 配置（读取身份/位置/飞手/实名登记号等，message_counter 自增）
 * @param out 输出缓冲区
 * @param max_len 缓冲区大小（建议 >= 96）
 * @return 实际写入字节数，0 = 失败
 */
uint16_t crid_build_gb46750_ie(cn_crid_config_t *config,
                               uint8_t *out, uint16_t max_len);

// 时间校准接口已统一到 crid_time.h（GB42590 / GB46750 共用同一基准）：
//     void     crid_time_set_epoch_ms(uint64_t epoch_ms);
//     uint64_t crid_time_epoch_ms(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_GB46750_H
