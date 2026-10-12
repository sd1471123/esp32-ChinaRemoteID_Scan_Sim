/**
 * @file crid_web.c
 * @brief 内置配置网页服务器：手机连接热点后访问 http://192.168.4.1
 *        可在线修改 UAS ID / 坐标 / 高度 / 速度 / 巡游参数 / 热点参数
 */

#include "crid_web.h"
#include "crid_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CN_C-RID_WEB";

static cn_crid_config_t *s_config = NULL;

// ================================================================
// 内置网页（全部使用单引号，避免 C 字符串转义）
// ================================================================
static const char PAGE_HTML[] =
"<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>C-RID 发射器配置</title><style>"
"body{font-family:sans-serif;margin:12px;background:#f5f6f7;color:#222}"
"h2{margin:8px 0}h3{margin:4px 0;font-size:15px}"
".card{background:#fff;border-radius:8px;padding:12px;margin-bottom:12px;"
"box-shadow:0 1px 3px rgba(0,0,0,.1)}"
"label{display:block;font-size:13px;color:#555;margin-top:8px}"
"input,select{width:100%;box-sizing:border-box;padding:8px;margin-top:3px;"
"border:1px solid #ccc;border-radius:6px;font-size:15px}"
"button{width:100%;padding:12px;margin-top:14px;border:0;border-radius:8px;"
"background:#0a7d43;color:#fff;font-size:16px}"
"#msg{margin-top:10px;font-size:14px;min-height:18px}"
".g{display:grid;grid-template-columns:1fr 1fr;gap:0 10px}"
"</style></head><body>"
"<h2>C-RID 发射器配置</h2>"
"<div class='card'><h3>身份信息</h3>"
"<label>无人机唯一 ID (UAS ID)</label><input id='uas_id' maxlength='24'>"
"<label>机型名称 (Self-ID)</label><input id='drone_name' maxlength='20'>"
"<label>操作员 ID</label><input id='operator_id' maxlength='20'>"
"<label>热点名称 (SSID)</label><input id='ssid' maxlength='31'>"
"<div class='g'>"
"<div><label>Wi-Fi 信道 (1-13)</label><input id='channel' type='number' min='1' max='13'></div>"
"<div><label>广播间隔 (ms)</label><input id='beacon_interval_ms' type='number' min='200' max='60000'></div>"
"</div></div>"
"<div class='card'><h3>飞行参数</h3><div class='g'>"
"<div><label>基准纬度</label><input id='base_latitude' type='number' step='0.0000001'></div>"
"<div><label>基准经度</label><input id='base_longitude' type='number' step='0.0000001'></div>"
"<div><label>海拔高度 m (MSL)</label><input id='base_altitude_msl' type='number'></div>"
"<div><label>相对高度 m (AGL)</label><input id='altitude_agl' type='number'></div>"
"<div><label>水平速度 m/s</label><input id='speed_horizontal' type='number' step='0.1'></div>"
"<div><label>运行状态</label><select id='status'>"
"<option value='0'>未声明</option><option value='1'>地面</option>"
"<option value='2'>空中</option><option value='3'>紧急</option>"
"<option value='4'>识别故障</option></select></div>"
"</div></div>"
"<div class='card'><h3>巡游参数</h3><div class='g'>"
"<div><label>巡游速度系数</label><input id='patrol_speed' type='number' step='0.01'></div>"
"<div><label>纬度半径 (度)</label><input id='patrol_radius_lat' type='number' step='0.00001'></div>"
"<div><label>经度半径 (度)</label><input id='patrol_radius_lon' type='number' step='0.00001'></div>"
"</div></div>"
"<button onclick='save()'>保存并应用</button>"
"<div id='msg'></div>"
"<p style='font-size:12px;color:#888'>手机连接本机热点后访问 http://192.168.4.1 打开此页。"
"修改 SSID / 信道 / 广播间隔保存后需重新连接热点。配置已持久化，断电不丢失。</p>"
"<script>"
"const F=['uas_id','drone_name','operator_id','ssid','channel','beacon_interval_ms',"
"'base_latitude','base_longitude','base_altitude_msl','altitude_agl','speed_horizontal',"
"'status','patrol_speed','patrol_radius_lat','patrol_radius_lon'];"
"function msg(t,ok){const m=document.getElementById('msg');m.textContent=t;"
"m.style.color=ok?'#0a7d43':'#c0392b';}"
"async function load(){try{const r=await fetch('/api/config');const j=await r.json();"
"F.forEach(k=>{if(j[k]!==undefined&&j[k]!==null)document.getElementById(k).value=j[k]});}"
"catch(e){msg('加载失败: '+e,false);}}"
"async function save(){const d={};F.forEach(k=>d[k]=document.getElementById(k).value);"
"try{const r=await fetch('/api/config',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});"
"const j=await r.json();"
"if(j.ok){msg('已保存并应用 ✓'+(j.restarted?' (热点参数已更新，如 SSID/信道变化请重新连接)':''),true);}"
"else{msg('保存失败: '+(j.err||''),false);}}"
"catch(e){msg('保存失败: '+e,false);}}"
"load();"
"</script></body></html>";

// ================================================================
// URI 处理
// ================================================================

static esp_err_t handler_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_get_config(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cJSON *j = cJSON_CreateObject();
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(j, "uas_id", cfg->uas_id);
    cJSON_AddStringToObject(j, "drone_name", cfg->drone_name);
    cJSON_AddStringToObject(j, "operator_id", cfg->operator_id);
    cJSON_AddStringToObject(j, "ssid", cfg->ssid);
    cJSON_AddNumberToObject(j, "channel", cfg->channel);
    cJSON_AddNumberToObject(j, "beacon_interval_ms", cfg->beacon_interval_ms);
    cJSON_AddNumberToObject(j, "base_latitude", cfg->base_latitude);
    cJSON_AddNumberToObject(j, "base_longitude", cfg->base_longitude);
    cJSON_AddNumberToObject(j, "base_altitude_msl", cfg->base_altitude_msl);
    cJSON_AddNumberToObject(j, "altitude_agl", cfg->altitude_agl);
    cJSON_AddNumberToObject(j, "speed_horizontal", cfg->speed_horizontal);
    cJSON_AddNumberToObject(j, "status", cfg->status);
    cJSON_AddNumberToObject(j, "patrol_speed", cfg->patrol_speed);
    cJSON_AddNumberToObject(j, "patrol_radius_lat", cfg->patrol_radius_lat);
    cJSON_AddNumberToObject(j, "patrol_radius_lon", cfg->patrol_radius_lon);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 从 JSON 取浮点/整数（不存在则跳过）
static bool jget_num(const cJSON *j, const char *key, double *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(j, key);
    if (it == NULL || !cJSON_IsNumber(it)) return false;
    *out = cJSON_GetNumberValue(it);
    return true;
}

static esp_err_t handler_post_config(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }

    char *body = malloc(total + 1);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int received = 0, ret;
    while (received < total) {
        ret = httpd_req_recv(req, body + received, total - received);
        if (ret <= 0) { free(body); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv fail"); return ESP_FAIL; }
        received += ret;
    }
    body[total] = '\0';

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    cn_crid_config_t *cfg = s_config;
    cn_crid_config_t next = *cfg;       // 先在副本上校验，全部通过后才应用
    char err[64] = {0};
    double v;
    bool restarted = false;

    const cJSON *item;
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "uas_id")) != NULL && cJSON_IsString(item)) {
        strncpy(next.uas_id, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.uas_id[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "drone_name")) != NULL && cJSON_IsString(item)) {
        strncpy(next.drone_name, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.drone_name[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "operator_id")) != NULL && cJSON_IsString(item)) {
        strncpy(next.operator_id, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.operator_id[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "ssid")) != NULL && cJSON_IsString(item)) {
        const char *s = cJSON_GetStringValue(item);
        if (strlen(s) == 0) snprintf(err, sizeof(err), "SSID 不能为空");
        else { strncpy(next.ssid, s, CRID_SSID_MAX_LEN); next.ssid[CRID_SSID_MAX_LEN] = '\0'; }
    }
    if (err[0] == '\0' && jget_num(j, "channel", &v)) {
        if (v < 1 || v > 13) snprintf(err, sizeof(err), "信道须在 1-13");
        else next.channel = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "beacon_interval_ms", &v)) {
        if (v < 200 || v > 60000) snprintf(err, sizeof(err), "广播间隔须在 200-60000ms");
        else next.beacon_interval_ms = (uint16_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_latitude", &v)) {
        if (v < -90 || v > 90) snprintf(err, sizeof(err), "纬度须在 -90~90");
        else next.base_latitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_longitude", &v)) {
        if (v < -180 || v > 180) snprintf(err, sizeof(err), "经度须在 -180~180");
        else next.base_longitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_altitude_msl", &v)) next.base_altitude_msl = (float)v;
    if (err[0] == '\0' && jget_num(j, "altitude_agl", &v)) next.altitude_agl = (float)v;
    if (err[0] == '\0' && jget_num(j, "speed_horizontal", &v)) {
        if (v < 0 || v > 254) snprintf(err, sizeof(err), "速度须在 0~254 m/s");
        else next.speed_horizontal = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "status", &v)) {
        if (v < 0 || v > 4) snprintf(err, sizeof(err), "状态取值 0-4");
        else next.status = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "patrol_speed", &v)) {
        if (v < 0 || v > 10) snprintf(err, sizeof(err), "巡游速度系数须在 0~10");
        else next.patrol_speed = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "patrol_radius_lat", &v)) next.patrol_radius_lat = (float)v;
    if (err[0] == '\0' && jget_num(j, "patrol_radius_lon", &v)) next.patrol_radius_lon = (float)v;
    cJSON_Delete(j);

    if (err[0] != '\0') {
        httpd_resp_set_type(req, "application/json");
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", err);
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    // 校验通过，应用配置
    bool ap_changed = (strcmp(next.ssid, cfg->ssid) != 0) ||
                      (next.channel != cfg->channel) ||
                      (next.beacon_interval_ms != cfg->beacon_interval_ms);
    uint8_t saved_counter = cfg->message_counter;   // 计数器不回退
    *cfg = next;
    cfg->message_counter = saved_counter;
    // 同步当前显示位置
    cfg->latitude = cfg->base_latitude;
    cfg->longitude = cfg->base_longitude;
    cfg->altitude_msl = cfg->base_altitude_msl;

    if (ap_changed) {
        if (crid_wifi_update_ap(cfg->ssid, cfg->channel, cfg->beacon_interval_ms) == ESP_OK) {
            restarted = true;
        }
    }

    crid_config_save_nvs(cfg);
    ESP_LOGI(TAG, "Config updated via web: id=%s pos=(%.6f,%.6f) restarted=%d",
             cfg->uas_id, cfg->base_latitude, cfg->base_longitude, restarted);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, restarted ? "{\"ok\":true,\"restarted\":true}"
                                      : "{\"ok\":true,\"restarted\":false}");
    return ESP_OK;
}

// 浏览器会自动请求站点图标，返回 204 空响应避免 404 日志刷屏
static esp_err_t handler_favicon(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uris[] = {
    { .uri = "/",          .method = HTTP_GET,  .handler = handler_page,      .user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_GET,  .handler = handler_get_config,.user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_POST, .handler = handler_post_config,.user_ctx = NULL },
    { .uri = "/favicon.ico",.method = HTTP_GET, .handler = handler_favicon,   .user_ctx = NULL },
};

esp_err_t crid_web_start(cn_crid_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = config;

    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 8;
    http_cfg.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &http_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "Web config server started: http://192.168.4.1");
    return ESP_OK;
}
