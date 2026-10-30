/**
 * @file crid_patrol.c
 * @brief 巡游轨迹生成（速度自洽的盘旋 + 缓变 GNSS 噪声 + 气压漂移）
 *
 * 设计要点：
 *
 * 1) 速度定半径/角速度
 *    旧实现是「固定半径 5.5 m + 每 10 秒 +0.2 rad」，屏幕上看是一段漂亮的
 *    椭圆，但线速度只有 0.08~0.11 m/s，报文里报的却是 1.0 m/s。任何做
 *    「位置差分速度 vs 报告速度」一致性检查的接收机都会直接判定合成轨迹。
 *    现在反过来以用户填写的水平速度为基准：
 *        ω = 0.15 rad/s（默认盘旋率，约 8.6°/s）
 *        R = v / ω        （速度越大半径越大，转弯率保持恒定）
 *    若 R 小于下限则钉在下限，同时把 ω 降到 v/R_MIN，仍然保证 v = R·ω。
 *    这样无论速度调到多少，位移都和广播速度对得上。
 *
 * 2) 位移必须用实测 dt
 *    位置推进由主任务按 1 Hz 触发，但实际调度会有抖动，所以用调用方传入的
 *    真实间隔算角度增量，避免长时间运行后累计漂移。
 *
 * 3) GNSS 噪声要「缓变」
 *    真实 GNSS 误差来自多径、电离层、卫星几何变化，短时间内高度相关；
 *    每帧独立的白噪声会让二阶差分爆掉，反而比不加噪声更像合成数据。
 *    这里用一阶马尔可夫过程：n = n*ρ + U(-1,1)*amp*(1-ρ)。
 *
 * 4) 气压高度不能恒等于几何高度
 *    真实气压计受气温、气流影响会缓慢游走（几分钟到几十分钟量级）。
 *    旧实现直接用「用户填的气压高度 + 高度偏移」，两者差值恒定，
 *    是个明显的人造特征。
 */

#include "crid_patrol.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "CN_C-RID_PATROL";

// 默认盘旋角速度（rad/s）。8.6°/s 对多旋翼是常见值：1 m/s 对应 6.7 m 半径，
// 15 m/s 对应 100 m 半径，曲率始终在物理合理范围内。
#define PATROL_OMEGA        0.15f
// 盘旋半径下限（米）。速度极低时若继续按 R=v/ω 缩小会退化成原地抖动，
// 钉在下限并降低 ω 更自然（慢速小半径盘旋）。
#define PATROL_RADIUS_MIN   5.5f
// 高度波动的角频率（rad/s）。0.01 → 周期 2π/0.01 ≈ 628 s（约 10.5 分钟），
// 与旧版「每 10 秒相位 +0.1」等价，但改成按秒推进后周期保持不变。
#define ALT_PHASE_RATE      0.01f
// 纬度 1° 对应米数（经度要再乘 cos(lat)）
#define M_PER_DEG_LAT       111320.0f
// 悬停判定阈值（m/s）：低于此值认为原地悬停，位置不再绕圈
#define HOVER_SPEED         0.05f

// 噪声幅度（米）。水平 0.25 m 低于报文里声明的「<1 m」精度，
// 高度 0.30 m 低于「<1 m」，不会自相矛盾。
#define NOISE_H             0.25f
#define NOISE_V             0.30f
// 一阶马尔可夫相关系数：越接近 1 变化越慢（时间常数 ≈ dt/(1-ρ) 秒）
#define NOISE_RHO_H         0.85f
#define NOISE_RHO_V         0.90f

// 气压漂移：随机游走，限制在 ±1.5 m
#define BARO_RHO            0.995f
#define BARO_STEP           0.02f
#define BARO_LIMIT          1.5f

static float s_n_h   = 0.0f;    // 北向水平噪声（米）
static float s_n_e   = 0.0f;    // 东向水平噪声（米）
static float s_n_alt = 0.0f;    // 高度噪声（米）
static float s_baro  = 0.0f;    // 气压漂移（米）
static bool  s_init  = false;

static inline float rnd_pm1(void) {
    // esp_random 是硬件真随机源；映射到 -1.0 ~ +1.0
    return (float)(esp_random() % 20001u) / 10000.0f - 1.0f;
}

static inline float markov(float *state, float amp, float rho) {
    *state = *state * rho + rnd_pm1() * amp * (1.0f - rho);
    return *state;
}

void crid_patrol_reset(void) {
    s_n_h = s_n_e = s_n_alt = s_baro = 0.0f;
    s_init = false;
}

void crid_patrol_step(cn_crid_config_t *config, float dt_sec) {
    if (config == NULL) return;
    // 保护：调度抖动或异常调用不应产生巨大位移
    if (!(dt_sec > 0.0f) || dt_sec > 10.0f) dt_sec = 1.0f;

    config->time_counter += dt_sec;

    float v = config->speed_horizontal;
    if (!(v > 0.0f)) v = 0.0f;

    bool hover = (v < HOVER_SPEED);
    if (hover) {
        v = 0.0f;
        config->speed_horizontal = 0.0f;   // 广播 0，与「位置不动」一致
    }

    // --- 速度 -> 半径 / 角速度 ---
    float r = config->patrol_radius_m;      // 悬停时沿用上一次半径，位置不跳回基准点
    if (!hover) {
        r = v / PATROL_OMEGA;
        if (r < PATROL_RADIUS_MIN) r = PATROL_RADIUS_MIN;
        float omega = v / r;               // v = r·ω 恒等
        config->patrol_theta += omega * dt_sec;
        config->patrol_radius_m = r;
    }
    // 相位归一化，避免长时间运行后 float 精度退化
    const float two_pi = 2.0f * (float)M_PI;
    if (config->patrol_theta >= two_pi) config->patrol_theta -= two_pi;
    if (config->patrol_theta < 0.0f)    config->patrol_theta += two_pi;

    float th = config->patrol_theta;

    // --- GNSS 噪声（缓变） ---
    if (!s_init) { s_n_h = s_n_e = s_n_alt = s_baro = 0.0f; s_init = true; }
    float n_h = markov(&s_n_h,   NOISE_H, NOISE_RHO_H);   // 北向（米）
    float n_e = markov(&s_n_e,   NOISE_H, NOISE_RHO_H);   // 东向（米）
    float n_a = markov(&s_n_alt, NOISE_V, NOISE_RHO_V);   // 高度（米）

    // --- 位置：以基准点为圆心的盘旋圆弧 ---
    float lat_rad = config->base_latitude * (float)M_PI / 180.0f;
    float m_per_deg_lon = M_PER_DEG_LAT * cosf(lat_rad);
    if (m_per_deg_lon < 1.0f) m_per_deg_lon = 1.0f;

    float north_m = r * cosf(th) + n_h;
    float east_m  = r * sinf(th) + n_e;

    float new_lat = config->base_latitude + north_m / M_PER_DEG_LAT;
    float new_lon = config->base_longitude + east_m / m_per_deg_lon;

    // --- 高度 ---
    float phase = config->time_counter * ALT_PHASE_RATE;
    float alt_off = config->altitude_wave * sinf(phase);
    float new_alt_msl = config->base_altitude_msl + alt_off + n_a;
    float new_alt_agl = new_alt_msl - config->ground_altitude;

    // --- 气压高度漂移（几何高度相对本机气压基准的残差） ---
    s_baro = s_baro * BARO_RHO + rnd_pm1() * BARO_STEP;
    if (s_baro >  BARO_LIMIT) s_baro =  BARO_LIMIT;
    if (s_baro < -BARO_LIMIT) s_baro = -BARO_LIMIT;
    config->baro_drift = s_baro;

    // --- 垂直速度：高度波动对时间的导数（与高度变化同源，不另编数值） ---
    // 幅度 5 m / 周期 628 s → 峰值 0.05 m/s，落在 0.5 m/s 的编码精度以下，
    // 广播出来就是 0——这与真机缓慢升降时被量化成 0 的表现一致。
    float new_speed_v = config->speed_vertical;
    if (config->vspd_mode == 0) {
        new_speed_v = config->altitude_wave * ALT_PHASE_RATE * cosf(phase);
    }

    // --- 航向：圆弧切线方向（正北 0°，顺时针） ---
    // dP/dθ = (-sin θ, cos θ)  →  (北, 东) = (-sin θ, cos θ)
    float new_heading = config->heading;
    if (config->heading_mode == 0 && !hover && r > 0.0f) {
        float hd = atan2f(cosf(th), -sinf(th)) * 180.0f / (float)M_PI;
        if (hd < 0.0f) hd += 360.0f;
        new_heading = hd;
    }

    crid_config_update_position(config, new_lat, new_lon,
                                new_alt_msl, new_alt_agl,
                                v, new_speed_v,
                                new_heading);

    ESP_LOGD(TAG, "R=%.1fm th=%.1fdeg pos=(%.6f,%.6f) alt=%.1f(agl %.1f) hdg=%.1f v=%.2f/%.2f drift=%.2f",
             r, th * 180.0f / (float)M_PI, new_lat, new_lon, new_alt_msl,
             new_alt_agl, new_heading, v, new_speed_v, config->baro_drift);
}
