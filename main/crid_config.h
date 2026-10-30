#ifndef CRID_CONFIG_H
#define CRID_CONFIG_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- 报文类型 (符合 ASTM F3411 / ASD-STAN 4709-002) ---
#define MSG_TYPE_BASIC_ID    0x0  // 基本 ID 报文
#define MSG_TYPE_LOCATION    0x1  // 位置向量报文
#define MSG_TYPE_AUTH        0x2  // 认证报文
#define MSG_TYPE_SELF_DESC   0x3  // 运行描述报文
#define MSG_TYPE_SYSTEM      0x4  // 系统报文
#define MSG_TYPE_OPERATOR_ID 0x5  // 操作员 ID 报文
#define MSG_TYPE_PACKED      0xF  // 报文打包

// --- ID 类型 ---
#define ID_TYPE_NONE             0
#define ID_TYPE_SERIAL_NUMBER    1
#define ID_TYPE_CAA_REGISTRATION 2
#define ID_TYPE_UTM_ASSIGNED     3
#define ID_TYPE_SPECIFIC_SESSION 4

// --- UA 类型 ---
#define UA_TYPE_NONE             0
#define UA_TYPE_AEROPLANE        1
#define UA_TYPE_HELICOPTER       2
#define UA_TYPE_GYROPLANE        3
#define UA_TYPE_HYBRID_LIFT      4
#define UA_TYPE_ORNITHOPTER      5
#define UA_TYPE_GLIDER           6
#define UA_TYPE_KITE             7
#define UA_TYPE_FREE_BALLOON     8
#define UA_TYPE_CAPTIVE_BALLOON  9
#define UA_TYPE_AIRSHIP          10
#define UA_TYPE_FALL_AWAY        11
#define UA_TYPE_ROCKET           12
#define UA_TYPE_TETHERED         13
#define UA_TYPE_GROUND_OBSTACLE  14
#define UA_TYPE_OTHER            15

// --- 运行状态 (ASTM F3411) ---
#define STATUS_UNDECLARED         0
#define STATUS_GROUND             1
#define STATUS_AIRBORNE           2
#define STATUS_EMERGENCY          3
#define STATUS_REMOTE_ID_FAIL     4

// --- 高度参考类型 ---
#define HEIGHT_REF_OVER_TAKEOFF   0
#define HEIGHT_REF_OVER_GROUND    1

// --- 操作员位置类型 ---
#define OP_LOC_TYPE_TAKEOFF       0
#define OP_LOC_TYPE_LIVE_GNSS     1
#define OP_LOC_TYPE_FIXED         2

// --- 分类类型 ---
#define CLASSIFICATION_UNDECLARED 0
#define CLASSIFICATION_EU         1

// --- 描述类型 ---
#define DESC_TYPE_TEXT            0
#define DESC_TYPE_EMERGENCY       1
#define DESC_TYPE_EXTENDED_STATUS 2

// --- 报文常量 ---
#define CRID_MESSAGE_SIZE      25     // 每条报文 25 字节
#define CRID_UAS_ID_MAX_LEN    20     // UAS ID 最大长度
#define CRID_SSID_MAX_LEN      31     // SSID 最大长度

// --- 热点密码（WPA2-PSK）---
// 802.11 规定 WPA2 个人版口令为 8-63 个可打印 ASCII 字符；留空表示开放热点。
// 默认给一个固定口令，出厂即加密——否则任何人都能连上热点往设备里刷固件。
#define CRID_AP_PASS_MAX_LEN   63
#define CRID_AP_PASS_MIN_LEN   8
#define CRID_AP_PASS_DEFAULT   "sd1471123"

// --- 中国 C-RID 标准 OUI 和类型 ---
#define CRID_OUI_0 0xFA
#define CRID_OUI_1 0x0B
#define CRID_OUI_2 0xBC
#define CRID_VENDOR_TYPE 0x0D           // ASTM / GB42590（OpenDroneID Message Pack）
#define CRID_VENDOR_TYPE_GB46750 0xF0   // GB46750-2025 新国标（数据类型 255 位图报文）

// --- 设备工作模式 ---
#define DEVICE_MODE_TX 0            // 发射模式（广播 RID）
#define DEVICE_MODE_RX 1            // 接收模式（监听周边无人机 RID）

// --- Wi-Fi 频段 ---
// 只有 ESP32-C5 系列带 5 GHz 射频；S3 等 2.4G-only 芯片在编译期就把相关
// 代码与页面选项裁掉，避免链接到不存在的 esp_wifi_set_band_mode()。
#if defined(CONFIG_IDF_TARGET_ESP32C5)
#define CRID_HAS_5G_BAND 1
#else
#define CRID_HAS_5G_BAND 0
#endif

#define CRID_BAND_2G 0              // 2.4 GHz（GB42590: 2400-2476 MHz）
#define CRID_BAND_5G 1              // 5.8 GHz（GB42590: 5725-5829 MHz）

// GB 42590 A.1.3.1.3：采用固定信道时 5.8 GHz 应为 149（中心频点 5745 MHz）。
// 149 只作为默认值；页面允许在 149-165 内自由选（20 MHz 网格，步进 4）。
#define CRID_5G_CH_DEFAULT 149
#define CRID_5G_CH_MIN     149      // 5725-5829 MHz 对应 149-165
#define CRID_5G_CH_MAX     165
#define CRID_5G_CH_STEP    4        // 相邻 20 MHz 信道的编号差（149/153/157/161/165）

// 单射频约束：2.4G 与 5.8G 不能同时工作，同一时刻只有一个频段是「活跃」的。
// 两个频段的开关各自独立保存，但只有活跃频段那一组真正生效。

// --- 广播标准 ---
#define RID_STD_GB42590 0           // 旧国标 GB 42590-2023（OpenDroneID 打包报文）
#define RID_STD_GB46750 1           // 新国标 GB 46750-2025（位图数据项报文）

// --- 默认配置 ---
#define DEFAULT_WIFI_CHANNEL       6
// Beacon 发送间隔：1 秒一次
#define DEFAULT_BEACON_INTERVAL_MS 1000
#define DEFAULT_MAX_FRAME_SIZE     512

// --- 配置结构体 ---
typedef struct {
    char uas_id[CRID_UAS_ID_MAX_LEN + 1];  // UAS ID / 无人机唯一标识 (序列号，填入 Basic ID 报文)
    uint8_t id_type;                         // ID 类型 (0-4)
    uint8_t ua_type;                         // 无人机类型 (0-15)
    char drone_name[CRID_UAS_ID_MAX_LEN + 1]; // 无人机型号 (Self-ID 描述报文)
    float latitude;                          // 纬度
    float longitude;                         // 经度
    float altitude_msl;                      // 海拔高度 (m)
    float altitude_agl;                      // 相对地面高度 (m)
    float speed_horizontal;                  // 水平速度 (m/s)
    float speed_vertical;                    // 垂直速度 (m/s)
    float heading;                           // 航向 (度)
    uint8_t status;                          // 运行状态 (0-4)
    float operator_lat;                      // 操作员纬度
    float operator_lon;                      // 操作员经度
    float operator_alt;                      // 操作员高度 (m)
    char operator_id[CRID_UAS_ID_MAX_LEN + 1]; // 操作员 ID
    uint8_t operator_location_type;          // 操作员位置类型 (0=Takeoff, 1=Live GNSS, 2=Fixed)
    uint8_t classification_type;             // 分类类型 (0=Undeclared, 1=EU)
    uint8_t category_eu;                     // EU 类别 (0=Undeclared, 1=Open, 2=Specific, 3=Certified)
    uint8_t class_eu;                        // EU 等级 (0-7)
    uint8_t height_type;                     // 高度参考类型 (0=Takeoff, 1=Ground)
    uint8_t mac_address[6];                  // MAC 地址
    char ssid[CRID_SSID_MAX_LEN + 1];        // SSID
    char ap_password[CRID_AP_PASS_MAX_LEN + 1];  // 热点密码(WPA2-PSK, 8-63 位, 空=开放热点)
    uint8_t channel;                         // 通道
    uint8_t message_counter;                 // 消息计数器 (0-255, 循环)
    uint16_t beacon_interval_ms;             // 广播间隔 (ms, 200-60000)

    // 模式与标准
    uint8_t device_mode;                     // 兼容字段 (0=发射, 1=接收)，由 tx/rx 开关推导
    uint8_t rid_standard;                    // 广播标准 (0=GB42590, 1=GB46750)
    char realname_id[9];                     // 实名登记号后 8 位 (GB46750 数据项 002, 空=不填)
    uint8_t ua_category_gb;                  // 新国标无人机分类 (0=微型,1=轻型,2=小型,3=中型,4=大型)

    // 频段与收发开关（两频段各自记忆，但单射频下只有 active band 生效）
    uint8_t band;                            // 当前活跃频段 (CRID_BAND_2G / CRID_BAND_5G)
    uint8_t tx_2g;                           // 2.4G 发射开关
    uint8_t rx_2g;                           // 2.4G 接收开关
    uint8_t tx_5g;                           // 5.8G 发射开关（仅 CRID_HAS_5G_BAND）
    uint8_t rx_5g;                           // 5.8G 接收开关（仅 CRID_HAS_5G_BAND）
    uint8_t channel_5g;                      // 5.8G 信道 (149-165，默认 149)

    // 巡游参数
    float base_latitude;                     // 基准纬度
    float base_longitude;                    // 基准经度
    float base_altitude_msl;                 // 基准海拔高度 (m)
    float patrol_radius_lat;                 // 纬度方向巡游半径
    float patrol_radius_lon;                 // 经度方向巡游半径
    float patrol_speed;                      // 巡游速度参数
    float time_counter;                      // 时间计数器

    // 高度模型：相对高度不再写死为「海拔 - 5」，改由地面海拔推导
    float ground_altitude;                   // 起飞点地面海拔 (m)，相对高度 = 海拔 - 该值
    float altitude_wave;                     // 高度波动幅度 (m)，0 = 高度完全固定

    // 气压高度（GB46750 数据项 014 / GB42590 Location 字节13）：
    // 默认与海拔一致（起飞点做气压基准的常见情形），可单独声明成真实气压高度
    float baro_altitude;
    // 运动参数来源：0 = 自动（航向跟随巡游轨迹、垂直速度跟随高度波动），
    // 1 = 采用用户填写的固定值，巡游不再改写
    uint8_t heading_mode;                    // 航向来源
    uint8_t vspd_mode;                       // 垂直速度来源

    // --- 运行时状态（不持久化，每次上电重新建立） ---
    // 轨迹相位角：半径与角速度由「水平速度」反算（R = v/ω），这个角随之推进，
    // 保证广播速度与相邻两帧位置差严格一致。
    float patrol_theta;
    // 当前盘旋半径（米）。速度变化时跟着变；速度归零（悬停）时保留，
    // 避免从盘旋切到悬停的瞬间位置弹回基准点。
    float patrol_radius_m;
    // 气压高度相对几何高度的缓慢漂移（米）。真实气压计受气温/气流影响会
    // 缓慢游走，恒定「气压高度 == 几何高度」是明显的合成特征。
    float baro_drift;
} cn_crid_config_t;

/**
 * @brief 初始化中国 C-RID 配置为默认值
 * @param config 指向配置结构体的指针
 */
void crid_config_init_default(cn_crid_config_t *config);

/**
 * @brief 将用户可编辑的配置保存到 NVS（断电保持）
 * @param config 指向配置结构体的指针
 * @return ESP_OK 成功
 */
esp_err_t crid_config_save_nvs(const cn_crid_config_t *config);

/**
 * @brief 从 NVS 加载已保存的配置（覆盖到 config 上，不存在的字段保持默认）
 * @param config 指向配置结构体的指针
 * @return true 加载到有效数据, false 无有效存储数据
 */
bool crid_config_load_nvs(cn_crid_config_t *config);

/**
 * @brief 更新位置数据
 * @param config 指向配置结构体的指针
 */
void crid_config_update_position(cn_crid_config_t *config,
                                 float lat, float lon,
                                 float alt_msl, float alt_agl,
                                 float speed_h, float speed_v,
                                 float heading);

/**
 * @brief 当前活跃频段是否开启发射
 */
uint8_t crid_band_tx_enabled(const cn_crid_config_t *config);

/**
 * @brief 当前活跃频段是否开启接收
 */
uint8_t crid_band_rx_enabled(const cn_crid_config_t *config);

/**
 * @brief 收发互斥归一化（单射频约束）
 *
 * 同一频段的发射与接收不能同时为 1（双开只能是分时轮转，会互相抢空口）；
 * 双开时保留发射，双关时给活跃频段补一个接收，保证设备至少有一路在工作。
 * 调用后会同步刷新 device_mode。
 */
void crid_config_radio_mutex(cn_crid_config_t *config);

/**
 * @brief 当前活跃频段对应的 Wi-Fi 信道
 *
 * 2.4G 用 config->channel（1-13）；5.8G 用 config->channel_5g（149-165，默认 149）。
 */
uint8_t crid_band_channel(const cn_crid_config_t *config);

/**
 * @brief 把 5.8G 信道规整到 149-165 的 20 MHz 网格（越界或非网格值回落 149）
 */
uint8_t crid_band_normalize_channel(uint8_t band, uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif // CRID_CONFIG_H
