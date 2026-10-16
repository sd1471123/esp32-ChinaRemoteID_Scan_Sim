/**
 * @file crid_rx.h
 * @brief 接收模式模块：混杂模式监听周边无人机 RID 广播并解析
 *
 * 在 softAP 保持运行的前提下开启 Wi-Fi 混杂模式（监听本机信道），
 * 捕获 Beacon / Probe Response 中的 RID 厂商 IE（OUI FA:0B:BC），
 * 支持 GB 42590-2023 打包报文（opendroneid 库解码）与
 * GB 46750-2025 位图报文（内置解码器）。
 *
 * 解析结果保存到内部航迹表（按源 MAC 分组，最多 8 机），供网页 /api/rx 查询。
 */

#ifndef CRID_RX_H
#define CRID_RX_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRID_RX_MAX_TRACKS 8      // 最多同时追踪的无人机数量

// 协议类型（与接收端固件 crid_rx_types.h 保持一致）
#define RX_PROTO_UNKNOWN   0
#define RX_PROTO_ASTM      1
#define RX_PROTO_GB42590   3
#define RX_PROTO_GB46750   4

// 单个被探测无人机的快照（网页展示用，从航迹表拷贝）
typedef struct {
    bool     active;
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  protocol;                // RX_PROTO_*
    char     uas_id[21];              // 唯一产品识别码 / 序列号
    char     drone_name[24];          // Self-ID 描述（机型名称）
    char     operator_id[21];         // 操作员 ID
    char     realname_id[9];          // 实名登记号后 8 位（仅 GB46750）
    uint8_t  ua_type;                 // 机型代码（GB46750 为分类 0-4）
    double   latitude;
    double   longitude;
    float    altitude_geo;            // 大地高度 m
    float    speed_horizontal;        // m/s
    float    speed_vertical;          // m/s
    float    direction;               // 航向 0-360°
    uint8_t  status;                  // 运行状态
    bool     has_op_loc;              // 飞手(遥控站)位置有效（广播中可能缺失）
    double   op_latitude;             // 飞手纬度
    double   op_longitude;            // 飞手经度
    float    op_altitude_geo;         // 飞手大地高度 m
    uint32_t msg_count;               // 累计报文数
    uint32_t last_seen_ms;            // 最后收到报文的系统时间 (esp_log_timestamp)
    uint32_t first_seen_ms;
} crid_rx_track_t;

/**
 * @brief 启动接收模式（开启混杂模式监听，创建解析任务）
 *
 * 要求 Wi-Fi 已初始化（softAP 运行中）。监听信道 = softAP 当前信道。
 *
 * @param listen_channel 监听信道 (1-13)
 * @return ESP_OK 成功
 */
esp_err_t crid_rx_start(uint8_t listen_channel);

/**
 * @brief 停止接收模式（关闭混杂模式，删除解析任务，清空航迹表）
 */
void crid_rx_stop(void);

/**
 * @brief 获取当前航迹表快照
 * @param out 输出数组（调用方分配，CRID_RX_MAX_TRACKS 个元素）
 * @return 活跃航迹数量
 */
int crid_rx_get_tracks(crid_rx_track_t *out);

/**
 * @brief 获取接收统计
 */
void crid_rx_get_stats(uint32_t *total_packets, uint32_t *rid_detections,
                       uint32_t *parse_ok, uint32_t *queue_overflows);

#ifdef __cplusplus
}
#endif

#endif // CRID_RX_H
