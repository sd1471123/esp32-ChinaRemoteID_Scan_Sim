/**
 * @file crid_gb46750.h
 * @brief GB 46750-2025 新国标运行识别报文构建器（位图数据项格式）
 *
 * 报文载体与 GB 42590 相同（Wi-Fi vendor IE，OUI FA:0B:BC / Type 0x0D），
 * IE payload 布局与接收端解析器（main_rx/crid_parser.c 策略1）对齐：
 *
 *   [MessageCounter(1)] [0xFF 数据类型(1)] [版本号(1)] [数据内容长度(1)]
 *   [数据标识 3 字节位图] [数据内容项(变长，按位图从高到低排列)]
 *
 * 版本号: 高 3 位 = 主版本 001(V1)，低 5 位 = 子版本号
 * 数据标识: 每字节 bit7..bit1 对应数据项存在标志，bit0 为扩展标志
 *
 * 本实现发送全部 13 项必选数据项（001/002/004/005/006/007/008/009/010/
 * 013/015/016/017/018/019/020/021），跳过可选项（003/011/012/014）。
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

// 必选项位图（全部 13+ 项必选内容，跳过可选项 003/011/012/014）
#define GB46750_FLAGS_BYTE0 (GB46750_BIT_UNIQUE_ID | GB46750_BIT_REALNAME_FLAG | \
                             GB46750_BIT_UA_CATEGORY | GB46750_BIT_RCS_LOC_TYPE | \
                             GB46750_BIT_RCS_LOCATION | GB46750_BIT_RCS_ALTITUDE)
#define GB46750_FLAGS_BYTE1 (GB46750_BIT_UAV_LOCATION | GB46750_BIT_TRACK_ANGLE | \
                             GB46750_BIT_GROUND_SPEED | GB46750_BIT_GEO_ALTITUDE)
#define GB46750_FLAGS_BYTE2 (GB46750_BIT_OP_STATUS | GB46750_BIT_COORD_SYSTEM | \
                             GB46750_BIT_H_ACCURACY | GB46750_BIT_V_ACCURACY | \
                             GB46750_BIT_SPEED_ACCURACY | GB46750_BIT_TIMESTAMP | \
                             GB46750_BIT_TS_ACCURACY)

// 版本号 V1.0: 高3位=001(主版本1), 低5位=子版本0
#define GB46750_VERSION_BYTE 0x20

/**
 * @brief 构建 GB 46750-2025 完整厂商 IE
 *
 * IE 结构: [0xDD][len][FA 0B BC][0x0D][counter][0xFF][ver][clen][flags3][content...]
 *
 * @param config 配置（读取身份/位置/飞手/实名登记号等，message_counter 自增）
 * @param out 输出缓冲区
 * @param max_len 缓冲区大小（建议 >= 96）
 * @return 实际写入字节数，0 = 失败
 */
uint16_t crid_build_gb46750_ie(cn_crid_config_t *config,
                               uint8_t *out, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif // CRID_GB46750_H
