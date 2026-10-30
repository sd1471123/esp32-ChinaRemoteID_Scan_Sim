#ifndef CRID_PATROL_H
#define CRID_PATROL_H

#include "crid_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 推进一个时间步长的巡游运动
 *
 * 轨迹是一段真正的盘旋圆弧（不是按固定半径套公式），半径与角速度由
 * 「水平速度」反算：R = v / ω，ω = v / R，两者相乘恒等于 v，因此广播出去
 * 的地速与相邻两帧的经纬度差严格一致。这一点是真机与合成轨迹最容易区分
 * 的地方——固定半径 + 固定速度会让「报告 1 m/s、实际只移动 0.1 米/秒」。
 *
 * 位置、高度上叠加缓变的一阶马尔可夫噪声（真实 GNSS 误差是相关的，
 * 白噪声反而假），气压高度叠加缓慢漂移。
 *
 * @param config 配置结构体指针（原地更新位置/高度/速度/航向）
 * @param dt_sec 距上次调用经过的秒数（按实测间隔传入，保证位移与速度自洽）
 */
void crid_patrol_step(cn_crid_config_t *config, float dt_sec);

/**
 * @brief 复位内部噪声状态（切换配置/重新开始模拟时调用）
 */
void crid_patrol_reset(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_PATROL_H
