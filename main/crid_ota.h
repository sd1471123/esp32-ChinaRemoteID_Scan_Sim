/**
 * @file crid_ota.h
 * @brief 网页内固件升级（OTA）：通过 HTTP 把编译产物 .bin 写入另一个 app 分区
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 生成本次上电的随机上传令牌（app_main 里调用一次即可）
void crid_ota_init(void);

/// GET /api/ota/info：当前版本、运行分区、可升级容量、上传令牌
esp_err_t crid_ota_handler_info(httpd_req_t *req);

/// POST /api/ota/upload：裸二进制流写入下一个空闲 app 分区，成功后重启
esp_err_t crid_ota_handler_upload(httpd_req_t *req);

/**
 * @brief 是否有 OTA 上传正在进行
 *
 * 供会话回收逻辑判断：一次 1.39MB 上传要用 11~12 秒，已超过「会话超龄」阈值，
 * 回收时若把它当成死连接关掉，用户看到的就是「传了一半突然失败/卡住」。
 * 上传期间必须整体跳过超龄清理。
 */
bool crid_ota_busy(void);

#ifdef __cplusplus
}
#endif
