#include "crid_patrol.h"
#include <math.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "CN_C-RID_PATROL";

void crid_patrol_step(cn_crid_config_t *config) {
    if (config == NULL) return;

    config->time_counter += 1.0f;

    float angle = config->time_counter * config->patrol_speed;

    // 圆形巡游路径
    float new_lat = config->base_latitude +
                    config->patrol_radius_lat * cosf(angle);
    float new_lon = config->base_longitude +
                    config->patrol_radius_lon * sinf(angle);

    // 高度周期性变化：幅度取「高度波动幅度」，填 0 时高度完全固定（alt_offset 恒 0）
    // 相位每步 +0.1 rad，而一步是 10 秒 => 周期 2π/0.1 = 62.8 步 ≈ 628 秒（约 10.5 分钟）
    float phase = config->time_counter * 0.1f;
    float alt_offset = config->altitude_wave * sinf(phase);
    float new_alt_msl = config->base_altitude_msl + alt_offset;
    // 相对高度由「海拔 - 起飞点地面海拔」推导（旧实现写死 new_alt_msl - 5，
    // 导致页面填的相对高度被无声丢弃）
    float new_alt_agl = new_alt_msl - config->ground_altitude;

    // 水平速度：巡游不再改写，保留用户在页面「水平速度」里填的值
    // （旧实现固定写成 1.5 + 0.5*sin，会每 10 秒把用户设置覆盖成 1.0~2.0 m/s，
    //   页面上填 15.166 而接收端只看到 1.4 就是这个原因）
    float new_speed_h = config->speed_horizontal;
    // 垂直速度 = 高度对时间的导数：d/dt[amp*sin(0.1t)] = amp*0.1*cos(0.1t)，t 以 10 秒为单位
    // => 峰值 = amp × 0.01 m/s。旧实现写死 0.5*cos，与 5 m 幅度实际 0.05 m/s 的速率差 10 倍
    float new_speed_v = config->altitude_wave * 0.1f * cosf(phase) / 10.0f;

    // 航向（基于运动切线方向，正北为0°，顺时针增加）
    // 位置: lat = base + r_lat*cos(angle), lon = base + r_lon*sin(angle)
    // d(lat)/dt = -r_lat*sin(angle)*omega, d(lon)/dt = r_lon*cos(angle)*omega
    // 航向 = atan2(dlon, dlat) （注意：atan2(x, y) 给出从y轴顺时针的角度）
    float dlat = -config->patrol_radius_lat * sinf(angle) * config->patrol_speed;
    float dlon = config->patrol_radius_lon * cosf(angle) * config->patrol_speed;
    float new_heading = atan2f(dlon, dlat) * 180.0f / M_PI;
    if (new_heading < 0.0f) new_heading += 360.0f;

    crid_config_update_position(config, new_lat, new_lon,
                                new_alt_msl, new_alt_agl,
                                new_speed_h, new_speed_v,
                                new_heading);

    ESP_LOGI(TAG, "Patrol step: pos=(%.6f,%.6f), alt=%.1fm(agl %.1fm), hdg=%.1f°, spd=%.1f/%.2fm/s",
             new_lat, new_lon, new_alt_msl, new_alt_agl, new_heading, new_speed_h, new_speed_v);
}
