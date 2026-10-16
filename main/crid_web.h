#ifndef CRID_WEB_H
#define CRID_WEB_H

#include "crid_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动内置配置网页服务器（http://192.168.4.1）
 *
 * 提供:
 *   GET  /          配置页面（手机浏览器直接打开）
 *   GET  /api/config 读取当前配置 (JSON)
 *   POST /api/config 保存并应用配置 (JSON)，同时写入 NVS
 *
 * @param config 指向全局配置结构体（保存/应用均直接修改它）
 * @return ESP_OK 成功
 */
esp_err_t crid_web_start(cn_crid_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // CRID_WEB_H
