#include "crid_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "CN_C-RID_CFG";

void crid_config_init_default(cn_crid_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "config is NULL");
        return;
    }

    memset(config, 0, sizeof(cn_crid_config_t));

    // --- 从硬件获取 MAC 地址 ---
    esp_err_t mac_ret = esp_efuse_mac_get_default(config->mac_address);
    if (mac_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address, using fallback");
        config->mac_address[0] = 0x24;
        config->mac_address[1] = 0x0A;
        config->mac_address[2] = 0xC4;
        config->mac_address[3] = 0x12;
        config->mac_address[4] = 0x34;
        config->mac_address[5] = 0x57;
    }

    // 提取 MAC 地址最后 4 位（即后 2 字节）作为后缀
    // 例如 MAC 24:0A:C4:12:34:56 -> 后缀 "3456"
    char mac_suffix[5];
    snprintf(mac_suffix, sizeof(mac_suffix), "%02X%02X",
             config->mac_address[4], config->mac_address[5]);

    // --- UAS ID / 无人机唯一标识: 前缀 "CRID-" + MAC 后 4 位 ---
    snprintf(config->uas_id, CRID_UAS_ID_MAX_LEN + 1, "ESP32-CRID-%s", mac_suffix);

    config->id_type = ID_TYPE_SERIAL_NUMBER;
    config->ua_type = UA_TYPE_HELICOPTER;

    // 越秀山坐标
    config->latitude = 23.14287f;
    config->longitude = 113.26026f;
    config->altitude_msl = 50.0f;
    // 高度模型：出厂默认认为起飞点地面海拔为 0，故相对高度与海拔同值
    config->ground_altitude = 0.0f;
    config->altitude_agl = config->altitude_msl - config->ground_altitude;
    config->altitude_wave = 5.0f;   // 巡游时高度上下摆动幅度（0 = 高度完全固定）
    config->speed_horizontal = 1.0f;
    config->speed_vertical = 0.0f;
    config->heading = 45.0f;
    config->status = STATUS_AIRBORNE;

    config->operator_lat = 23.14f;
    config->operator_lon = 113.26f;
    config->operator_alt = 10.0f;

    // 飞手名字: 前缀 "OP-CAAC-" + MAC 后 4 位
    snprintf(config->operator_id, CRID_UAS_ID_MAX_LEN + 1, "ESP32-OP-%s", mac_suffix);

    // 无人机名字/型号 (Self-ID 描述): 填写为 ESP32S3
    strncpy(config->drone_name, "ESP32S3", CRID_UAS_ID_MAX_LEN);
    config->drone_name[CRID_UAS_ID_MAX_LEN] = '\0';

    config->operator_location_type = OP_LOC_TYPE_LIVE_GNSS; // Dynamic
    config->classification_type = CLASSIFICATION_UNDECLARED;
    config->category_eu = 0;
    config->class_eu = 0;
    config->height_type = HEIGHT_REF_OVER_TAKEOFF;

    // SSID 后缀也用 MAC 后 4 位
    snprintf(config->ssid, CRID_SSID_MAX_LEN + 1, "ESP32-CRID-%s", mac_suffix);

    config->channel = DEFAULT_WIFI_CHANNEL;
    config->message_counter = 0;
    config->beacon_interval_ms = DEFAULT_BEACON_INTERVAL_MS;

    // 模式与标准
    config->device_mode = DEVICE_MODE_TX;
    config->rid_standard = RID_STD_GB42590;
    config->realname_id[0] = '\0';
    config->ua_category_gb = 1;   // 默认轻型

    // 频段与收发开关：单射频互斥，每个频段默认只开一路。
    // 出厂默认 2.4G 发射（与 device_mode 默认 TX 一致）；5.8G 组也默认发射，切过去即可用
    config->band = CRID_BAND_2G;
    config->tx_2g = 1;
    config->rx_2g = 0;
    config->tx_5g = 1;
    config->rx_5g = 0;
    config->channel_5g = CRID_5G_CH_DEFAULT;

    // 巡游参数
    config->base_latitude = config->latitude;
    config->base_longitude = config->longitude;
    config->base_altitude_msl = config->altitude_msl;
    config->patrol_radius_lat = 0.00005f;  // 约 5.5 米
    config->patrol_radius_lon = 0.00004f;  // 约 4.4 米
    config->patrol_speed = 0.2f;
    config->time_counter = 0.0f;

    ESP_LOGI(TAG, "China C-RID configuration initialized");
    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             config->mac_address[0], config->mac_address[1], config->mac_address[2],
             config->mac_address[3], config->mac_address[4], config->mac_address[5]);
    ESP_LOGI(TAG, "  UAS ID: %s", config->uas_id);
    ESP_LOGI(TAG, "  Drone Model (Self-ID): %s", config->drone_name);
    ESP_LOGI(TAG, "  Operator ID: %s", config->operator_id);
    ESP_LOGI(TAG, "  ID Type: %d (Serial Number)", config->id_type);
    ESP_LOGI(TAG, "  UA Type: %d", config->ua_type);
    ESP_LOGI(TAG, "  Position: %.6f, %.6f", config->latitude, config->longitude);
}

void crid_config_update_position(cn_crid_config_t *config,
                                  float lat, float lon,
                                  float alt_msl, float alt_agl,
                                  float speed_h, float speed_v,
                                  float heading) {
    if (config == NULL) return;

    config->latitude = lat;
    config->longitude = lon;
    config->altitude_msl = alt_msl;
    config->altitude_agl = alt_agl;
    config->speed_horizontal = speed_h;
    config->speed_vertical = speed_v;
    config->heading = heading;

    ESP_LOGI(TAG, "Position updated: %.6f, %.6f, Alt: %.2fm, Hdg: %.1f",
             config->latitude, config->longitude,
             config->altitude_msl, config->heading);
}

// ================================================================
// NVS 持久化（仅保存用户可编辑字段，版本化 blob）
// ================================================================

#define CRID_NVS_NAMESPACE "crid"
#define CRID_NVS_KEY_CFG   "cfg"

typedef struct __attribute__((packed)) {
    uint32_t magic;                              // 'CRID' = 0x44495243
    uint16_t version;                            // 结构版本
    char uas_id[CRID_UAS_ID_MAX_LEN + 1];
    char drone_name[CRID_UAS_ID_MAX_LEN + 1];
    char operator_id[CRID_UAS_ID_MAX_LEN + 1];
    char ssid[CRID_SSID_MAX_LEN + 1];
    float base_latitude;
    float base_longitude;
    float base_altitude_msl;
    float altitude_agl;
    float speed_horizontal;
    float patrol_radius_lat;
    float patrol_radius_lon;
    float patrol_speed;
    uint16_t beacon_interval_ms;
    uint8_t status;
    uint8_t channel;
} crid_persist_v1_t;

// v2: 新增模式/标准/实名登记号/新国标分类
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;                            // = 2
    char uas_id[CRID_UAS_ID_MAX_LEN + 1];
    char drone_name[CRID_UAS_ID_MAX_LEN + 1];
    char operator_id[CRID_UAS_ID_MAX_LEN + 1];
    char ssid[CRID_SSID_MAX_LEN + 1];
    float base_latitude;
    float base_longitude;
    float base_altitude_msl;
    float altitude_agl;
    float speed_horizontal;
    float patrol_radius_lat;
    float patrol_radius_lon;
    float patrol_speed;
    uint16_t beacon_interval_ms;
    uint8_t status;
    uint8_t channel;
    uint8_t device_mode;                         // 0=发射, 1=接收
    uint8_t rid_standard;                        // 0=GB42590, 1=GB46750
    char realname_id[9];                         // 实名登记号后 8 位
    uint8_t ua_category_gb;                      // 0-4
} crid_persist_v2_t;

// v3: 新增频段与 2.4G/5.8G 各自的收发开关（单射频，只有活跃频段生效）
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;                            // = 3
    char uas_id[CRID_UAS_ID_MAX_LEN + 1];
    char drone_name[CRID_UAS_ID_MAX_LEN + 1];
    char operator_id[CRID_UAS_ID_MAX_LEN + 1];
    char ssid[CRID_SSID_MAX_LEN + 1];
    float base_latitude;
    float base_longitude;
    float base_altitude_msl;
    float altitude_agl;
    float speed_horizontal;
    float patrol_radius_lat;
    float patrol_radius_lon;
    float patrol_speed;
    uint16_t beacon_interval_ms;
    uint8_t status;
    uint8_t channel;
    uint8_t device_mode;
    uint8_t rid_standard;
    char realname_id[9];
    uint8_t ua_category_gb;
    uint8_t band;                                // 0=2.4G, 1=5.8G
    uint8_t tx_2g;
    uint8_t rx_2g;
    uint8_t tx_5g;
    uint8_t rx_5g;
    uint8_t channel_5g;
} crid_persist_v3_t;

#define CRID_PERSIST_MAGIC   0x44495243UL
#define CRID_PERSIST_VERSION 4

// v4: 在 v3 之后追加「地面海拔」与「高度波动幅度」。
// 直接内嵌 v3 结构体，避免再抄一遍三十个字段；两者都是 packed，长度确定。
typedef struct __attribute__((packed)) {
    crid_persist_v3_t v3;
    float ground_altitude;                       // 起飞点地面海拔 (m)
    float altitude_wave;                         // 高度波动幅度 (m)
} crid_persist_v4_t;

esp_err_t crid_config_save_nvs(const cn_crid_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;

    crid_persist_v4_t p;
    memset(&p, 0, sizeof(p));
    p.v3.magic = CRID_PERSIST_MAGIC;
    p.v3.version = CRID_PERSIST_VERSION;
    strncpy(p.v3.uas_id, config->uas_id, CRID_UAS_ID_MAX_LEN);
    strncpy(p.v3.drone_name, config->drone_name, CRID_UAS_ID_MAX_LEN);
    strncpy(p.v3.operator_id, config->operator_id, CRID_UAS_ID_MAX_LEN);
    strncpy(p.v3.ssid, config->ssid, CRID_SSID_MAX_LEN);
    p.v3.base_latitude = config->base_latitude;
    p.v3.base_longitude = config->base_longitude;
    p.v3.base_altitude_msl = config->base_altitude_msl;
    p.v3.altitude_agl = config->altitude_agl;
    p.v3.speed_horizontal = config->speed_horizontal;
    p.v3.patrol_radius_lat = config->patrol_radius_lat;
    p.v3.patrol_radius_lon = config->patrol_radius_lon;
    p.v3.patrol_speed = config->patrol_speed;
    p.v3.beacon_interval_ms = config->beacon_interval_ms;
    p.v3.status = config->status;
    p.v3.channel = config->channel;
    p.v3.device_mode = config->device_mode;
    p.v3.rid_standard = config->rid_standard;
    strncpy(p.v3.realname_id, config->realname_id, sizeof(p.v3.realname_id) - 1);
    p.v3.ua_category_gb = config->ua_category_gb;
    p.v3.band = (config->band == CRID_BAND_5G) ? CRID_BAND_5G : CRID_BAND_2G;
    p.v3.tx_2g = config->tx_2g ? 1 : 0;
    p.v3.rx_2g = config->rx_2g ? 1 : 0;
    p.v3.tx_5g = config->tx_5g ? 1 : 0;
    p.v3.rx_5g = config->rx_5g ? 1 : 0;
    p.v3.channel_5g = config->channel_5g;
    p.ground_altitude = config->ground_altitude;
    p.altitude_wave = config->altitude_wave;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CRID_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_blob(handle, CRID_NVS_KEY_CFG, &p, sizeof(p));
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save config failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }
    return ret;
}

// 从持久化公共字段恢复（v1/v2 共有部分）
static void persist_apply_common(cn_crid_config_t *config, const crid_persist_v1_t *p) {
    strncpy(config->uas_id, p->uas_id, CRID_UAS_ID_MAX_LEN);
    config->uas_id[CRID_UAS_ID_MAX_LEN] = '\0';
    strncpy(config->drone_name, p->drone_name, CRID_UAS_ID_MAX_LEN);
    config->drone_name[CRID_UAS_ID_MAX_LEN] = '\0';
    strncpy(config->operator_id, p->operator_id, CRID_UAS_ID_MAX_LEN);
    config->operator_id[CRID_UAS_ID_MAX_LEN] = '\0';
    strncpy(config->ssid, p->ssid, CRID_SSID_MAX_LEN);
    config->ssid[CRID_SSID_MAX_LEN] = '\0';
    config->base_latitude = p->base_latitude;
    config->base_longitude = p->base_longitude;
    config->base_altitude_msl = p->base_altitude_msl;
    config->altitude_agl = p->altitude_agl;
    config->speed_horizontal = p->speed_horizontal;
    config->patrol_radius_lat = p->patrol_radius_lat;
    config->patrol_radius_lon = p->patrol_radius_lon;
    config->patrol_speed = p->patrol_speed;
    if (p->beacon_interval_ms >= 200 && p->beacon_interval_ms <= 60000) {
        config->beacon_interval_ms = p->beacon_interval_ms;
    }
    config->status = p->status;
    if (p->channel >= 1 && p->channel <= 13) {
        config->channel = p->channel;
    }
    // 同步当前显示位置到基准（首次巡游步进前的显示值）
    config->latitude = config->base_latitude;
    config->longitude = config->base_longitude;
    config->altitude_msl = config->base_altitude_msl;
}

bool crid_config_load_nvs(cn_crid_config_t *config) {
    if (config == NULL) return false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CRID_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return false;   // 无存储数据，使用默认值

    uint8_t raw[512];
    size_t len = sizeof(raw);
    ret = nvs_get_blob(handle, CRID_NVS_KEY_CFG, raw, &len);
    nvs_close(handle);
    if (ret != ESP_OK || len < sizeof(crid_persist_v1_t)) {
        ESP_LOGW(TAG, "No valid stored config, using defaults");
        return false;
    }

    const crid_persist_v1_t *p1 = (const crid_persist_v1_t *)raw;
    if (p1->magic != CRID_PERSIST_MAGIC) {
        ESP_LOGW(TAG, "Stored config magic mismatch, using defaults");
        return false;
    }

    // v4 的前半段与 v3 完全一致（内嵌 v3），所以 >=3 走同一套字段恢复
    if (p1->version >= 3 && len >= sizeof(crid_persist_v3_t)) {
        const crid_persist_v3_t *p3 = (const crid_persist_v3_t *)raw;
        persist_apply_common(config, (const crid_persist_v1_t *)raw);
        config->device_mode = (p3->device_mode == DEVICE_MODE_RX) ? DEVICE_MODE_RX : DEVICE_MODE_TX;
        if (p3->rid_standard <= RID_STD_GB46750) config->rid_standard = p3->rid_standard;
        strncpy(config->realname_id, p3->realname_id, sizeof(config->realname_id) - 1);
        config->realname_id[sizeof(config->realname_id) - 1] = '\0';
        if (p3->ua_category_gb <= 4) config->ua_category_gb = p3->ua_category_gb;

        config->band = (p3->band == CRID_BAND_5G) ? CRID_BAND_5G : CRID_BAND_2G;
#if !CRID_HAS_5G_BAND
        config->band = CRID_BAND_2G;     // 无 5G 射频的芯片强制回落到 2.4G
#endif
        config->tx_2g = p3->tx_2g ? 1 : 0;
        config->rx_2g = p3->rx_2g ? 1 : 0;
        config->tx_5g = p3->tx_5g ? 1 : 0;
        config->rx_5g = p3->rx_5g ? 1 : 0;
        config->channel_5g = crid_band_normalize_channel(CRID_BAND_5G, p3->channel_5g);
        // 单射频互斥 + 保底：归一化收发开关，避免历史配置里出现双开或全关
        crid_config_radio_mutex(config);
    } else if (p1->version == 2 && len >= sizeof(crid_persist_v2_t)) {
        const crid_persist_v2_t *p2 = (const crid_persist_v2_t *)raw;
        persist_apply_common(config, (const crid_persist_v1_t *)raw);
        config->device_mode = (p2->device_mode == DEVICE_MODE_RX) ? DEVICE_MODE_RX : DEVICE_MODE_TX;
        if (p2->rid_standard <= RID_STD_GB46750) config->rid_standard = p2->rid_standard;
        strncpy(config->realname_id, p2->realname_id, sizeof(config->realname_id) - 1);
        config->realname_id[sizeof(config->realname_id) - 1] = '\0';
        if (p2->ua_category_gb <= 4) config->ua_category_gb = p2->ua_category_gb;
        // v2 只有 TX/RX 二选一，迁移到 2.4G 组的对应开关
        config->band = CRID_BAND_2G;
        config->tx_2g = (p2->device_mode == DEVICE_MODE_RX) ? 0 : 1;
        config->rx_2g = (p2->device_mode == DEVICE_MODE_RX) ? 1 : 0;
    } else if (p1->version == 1) {
        persist_apply_common(config, p1);
    } else {
        ESP_LOGW(TAG, "Unknown config version %u, using defaults", p1->version);
        return false;
    }

    if (p1->version >= 4 && len >= sizeof(crid_persist_v4_t)) {
        const crid_persist_v4_t *p4 = (const crid_persist_v4_t *)raw;
        config->ground_altitude = p4->ground_altitude;
        config->altitude_wave = p4->altitude_wave;
    } else {
        // 旧存档（v1~v3）没有地面海拔与波动幅度：
        // 用「海拔 - 相对高度」反推地面海拔，保证升级后相对高度显示值不变；
        // 波动幅度取 5 m，与原实现里 ±5 m 的摆动保持一致
        config->ground_altitude = config->base_altitude_msl - config->altitude_agl;
        config->altitude_wave = 5.0f;
    }
    if (config->ground_altitude < -1000.0f) config->ground_altitude = -1000.0f;
    if (config->ground_altitude > 10000.0f) config->ground_altitude = 10000.0f;
    if (!(config->altitude_wave >= 0.0f)) config->altitude_wave = 0.0f;
    if (config->altitude_wave > 100.0f) config->altitude_wave = 100.0f;

    // 相对高度一律由「海拔 - 地面海拔」推导，避免存档里的旧值自相矛盾
    config->altitude_msl = config->base_altitude_msl;
    config->altitude_agl = config->altitude_msl - config->ground_altitude;

    ESP_LOGI(TAG, "Config loaded from NVS: %s @ %s (band=%s tx=%u rx=%u std=%u)",
             config->uas_id, config->ssid,
             config->band == CRID_BAND_5G ? "5.8G" : "2.4G",
             crid_band_tx_enabled(config), crid_band_rx_enabled(config),
             config->rid_standard);
    return true;
}

// ================================================================
// 频段辅助
// ================================================================

uint8_t crid_band_tx_enabled(const cn_crid_config_t *config) {
    if (config == NULL) return 0;
#if CRID_HAS_5G_BAND
    if (config->band == CRID_BAND_5G) return config->tx_5g ? 1 : 0;
#endif
    return config->tx_2g ? 1 : 0;
}

uint8_t crid_band_rx_enabled(const cn_crid_config_t *config) {
    if (config == NULL) return 0;
#if CRID_HAS_5G_BAND
    if (config->band == CRID_BAND_5G) return config->rx_5g ? 1 : 0;
#endif
    return config->rx_2g ? 1 : 0;
}

// 单射频互斥：一条射频链路同一时刻只能发或只能收。
// 双开在硬件上只能是分时轮转（发射时段到不了的信号收不到），故归一化为二选一。
void crid_config_radio_mutex(cn_crid_config_t *config) {
    if (config == NULL) return;
    if (config->tx_2g && config->rx_2g) config->rx_2g = 0;   // 双开保留发射
    if (config->tx_5g && config->rx_5g) config->rx_5g = 0;
    if (!crid_band_tx_enabled(config) && !crid_band_rx_enabled(config)) {
        if (config->band == CRID_BAND_5G) config->rx_5g = 1;  // 双关补接收，保证有一路在跑
        else config->rx_2g = 1;
    }
    config->device_mode = crid_band_tx_enabled(config) ? DEVICE_MODE_TX : DEVICE_MODE_RX;
}

uint8_t crid_band_normalize_channel(uint8_t band, uint8_t channel) {
    if (band == CRID_BAND_5G) {
        // 5.8 GHz 只允许 149-165，且必须落在 20 MHz 网格上（步进 4）
        if (channel < CRID_5G_CH_MIN || channel > CRID_5G_CH_MAX ||
            ((channel - CRID_5G_CH_MIN) % 4) != 0) {
            return CRID_5G_CH_DEFAULT;
        }
        return channel;
    }
    if (channel < 1 || channel > 13) return DEFAULT_WIFI_CHANNEL;
    return channel;
}

uint8_t crid_band_channel(const cn_crid_config_t *config) {
    if (config == NULL) return DEFAULT_WIFI_CHANNEL;
#if CRID_HAS_5G_BAND
    if (config->band == CRID_BAND_5G) {
        return crid_band_normalize_channel(CRID_BAND_5G, config->channel_5g);
    }
#endif
    return crid_band_normalize_channel(CRID_BAND_2G, config->channel);
}
