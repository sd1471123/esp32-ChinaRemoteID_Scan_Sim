/**
 * @file crid_log.h
 * @brief 接收端飞行轨迹持久化：flash 环形日志
 *
 * 不用文件系统。在 ridlog 分区（4 MB，data/0x40）里按 4 KB 扇区顺序追加
 * 64 字节定长记录，写满一圈回头覆盖最旧的扇区。这样做的好处：
 *   - 没有目录与元数据，掉电最多丢当前这一条；
 *   - 擦除以扇区为单位，整圈均摊磨损（1 条/秒的速率下每个扇区约 3 年才擦 10 万次）；
 *   - 不需要 SPIFFS/FATFS 那一套，代码与 RAM 占用都很小。
 *
 * 分区布局：
 *   sector 0        -> meta（magic + 代次 gen + crc），清空记录只改这里
 *   sector 1..N-1   -> 数据环，每扇区 64 条记录
 *
 * 清空（crid_log_clear）不擦全部扇区——1023 个扇区要擦 30 秒。改成把 meta
 * 里的 gen 加一：旧记录的 gen 不匹配，扫描时直接视为无效；随后写新记录时
 * 照常擦除目标扇区，旧数据自然被覆盖。
 */

#ifndef CRID_LOG_H
#define CRID_LOG_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRID_LOG_PART_NAME    "ridlog"
#define CRID_LOG_PART_SUBTYPE 0x40
#define CRID_LOG_REC_SIZE     64      // 定长记录，改动需同步 crid_log.c 的断言

// 记录标志位
#define CRID_LOG_F_AGL   0x01         // agl_m 有效
#define CRID_LOG_F_VSPD  0x02         // spd_v_dms 有效（否则为 0）
#define CRID_LOG_F_FIX   0x04         // 经纬度有效

/**
 * @brief 一条轨迹记录（64 字节，紧凑排列）
 *
 * 坐标用 1e-7 度定点（与 GB46750 报文一致），高度用米，速度用 0.1 m/s，
 * 航向用 0.1 度——都在 int16/uint16 范围内，省一半空间。
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;       // 'RLOG'
    uint32_t seq;         // 单调递增，用于排序与定位最新记录
    uint16_t gen;         // 代次（清空时 +1，旧代次记录全部作废）
    uint32_t t_unix;      // Unix 秒；0 = 设备未对时
    uint32_t tick_ms;     // 上电毫秒（未对时时按此排序）
    uint8_t  mac[6];      // 目标源 MAC
    char     uid[14];     // UAS ID 前 14 个字符（辅助识别，可能为空）
    int32_t  lat_e7;      // 纬度 × 1e7
    int32_t  lon_e7;      // 经度 × 1e7
    int16_t  alt_m;       // 大地高度 m
    int16_t  agl_m;       // 相对高度 m（flags 无 AGL 位时无意义）
    int16_t  spd_h_dms;   // 水平速度 0.1 m/s
    int16_t  spd_v_dms;   // 垂直速度 0.1 m/s
    uint16_t trk_ddeg;    // 航向 0.1 度（0-3599）
    uint8_t  ua_type;     // 机型 / 新国标分类
    uint8_t  status;      // 运行状态
    uint8_t  proto;       // RX_PROTO_*
    int8_t   rssi;        // 信号强度 dBm
    uint8_t  flags;       // CRID_LOG_F_*
    uint8_t  rsv;         // 对齐保留
    uint16_t crc;         // 前面所有字节的 CRC16-CCITT
} crid_log_rec_t;

/**
 * @brief 一个目标的汇总（网页列表用）
 */
typedef struct {
    uint8_t  mac[6];
    char     uid[15];
    uint32_t count;         // 本次上电以来记录的条数
    uint32_t oldest_unix;   // 最早一条的 Unix 秒（0=未对时）
    uint32_t newest_unix;   // 最新一条的 Unix 秒
    uint32_t newest_tick;   // 最新一条的上电毫秒
    int16_t  last_alt_m;
    int32_t  last_lat_e7;
    int32_t  last_lon_e7;
} crid_log_target_t;

/**
 * @brief 分区与日志统计
 */
typedef struct {
    bool     ready;         // 分区找到且初始化成功
    bool     enabled;       // 记录开关（上电默认关闭，网页授时后再打开）
    uint32_t total_bytes;   // 分区可用容量（不含 meta 扇区）
    uint32_t used_bytes;    // 已写满的扇区字节数（估算，用于进度条）
    uint32_t count;         // 已存条数（写满一圈后固定为容量上限）
    uint32_t capacity;      // 最多可存条数
    uint32_t oldest_unix;   // 最旧记录时间（0=未对时或无记录）
    uint32_t oldest_tick;   // 最旧记录的上电毫秒
    uint32_t newest_unix;
    uint32_t newest_tick;   // 最新记录的上电毫秒
    uint32_t dropped;       // 队列满丢弃的条数
} crid_log_stats_t;

/**
 * @brief 初始化分区、扫描写指针、启动写任务
 *
 * 应用启动时调用一次即可，与收/发模式无关（发射模式不会有记录写入）。
 */
esp_err_t crid_log_init(void);

/** @brief 模块是否可用（分区存在且初始化成功） */
bool crid_log_ready(void);

/**
 * @brief 记录开关（只存在 RAM 里，不落 NVS）
 *
 * 上电默认关闭：设备没有 RTC，也不联网，未授时前写下的记录 time 字段只能是
 * 0（未对时），翻看时全是 1970。改成「打开网页 → 网页授时 → 再开始记录」，
 * 这样每一条落地记录都带真实时间。开关不持久化，断电重启后仍是关闭状态，
 * 由网页在每次加载时按用户上次的选择重新打开。
 */
void crid_log_set_enabled(bool on);

/** @brief 当前是否在记录 */
bool crid_log_enabled(void);

/**
 * @brief 投递一条记录（非阻塞，队列满直接丢弃并计数）
 *
 * 会在内部补齐 magic / gen / seq / crc，调用方只需填业务字段。
 */
void crid_log_append(const crid_log_rec_t *r);

/** @brief 读取统计信息 */
void crid_log_get_stats(crid_log_stats_t *out);

/**
 * @brief 取目标汇总列表（按最后出现时间倒序）
 * @param out       输出数组
 * @param max       数组容量
 * @return 实际条目数
 */
int crid_log_targets(crid_log_target_t *out, int max);

/**
 * @brief 取某个目标的轨迹点（从新到旧），支持分页
 * @param mac       目标 MAC；传 NULL 表示不过滤
 * @param skip      跳过最新的多少条（分页偏移，网页第 N 页传 N*每页条数）
 * @param limit     最多返回条数
 * @param out       输出数组（调用方分配 limit 个元素）
 * @param scan_max  最多回扫多少条（防止在大分区里空转）；传 0 表示扫全部
 * @param matched   可选输出：回扫范围内命中的总条数（用于算总页数）；
 *                  传 NULL 则命中 limit 条即提前结束，不做统计
 * @return 实际写入 out 的条数
 */
int crid_log_points(const uint8_t mac[6], uint32_t skip, uint32_t limit,
                    crid_log_rec_t *out, uint32_t scan_max, uint32_t *matched);

/** 遍历回调：返回 false 可提前终止 */
typedef bool (*crid_log_cb_t)(const crid_log_rec_t *r, void *ctx);

/**
 * @brief 按时间顺序（旧 -> 新）遍历全部有效记录
 *
 * 逐个扇区整块读 4 KB，65K 条约 1~2 秒；回调里可以直接往 HTTP chunk 写。
 */
void crid_log_foreach(crid_log_cb_t cb, void *ctx);

/** @brief 清空全部记录（只改写 meta 的代次，毫秒级完成） */
esp_err_t crid_log_clear(void);

/**
 * @brief 删除单个目标的全部历史记录
 *
 * 不擦 flash：给这个 MAC 记一个 seq 上界（墓碑），seq 不超过上界的记录在扫描、
 * 分页、导出时一律视为不存在；墓碑落 NVS，重启后依然有效（否则上电从 flash
 * 重建目标表时，被删掉的无人机又会冒出来）。删除后立刻重算总条数与各目标计数。
 *
 * @param mac  目标源 MAC（6 字节）
 */
esp_err_t crid_log_delete_target(const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif // CRID_LOG_H
