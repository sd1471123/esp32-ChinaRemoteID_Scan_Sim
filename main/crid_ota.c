/**
 * @file crid_ota.c
 * @brief 网页内固件升级（OTA）
 *
 * 设计要点（每一条都对应一个实际会踩的坑）：
 *  1. 裸流上传（application/octet-stream）而不是 multipart/form-data：
 *     multipart 需要在设备端解析边界、把文件块从表单里剥出来，800KB 的包
 *     多一次拷贝与解析，出错面更大；前端用 XHR 直接 send(File) 即可，
 *     Content-Length 自动带上，还能拿到上传进度。
 *  2. 只写「另一个」app 分区（esp_ota_get_next_update_partition），
 *     写完 esp_ota_end 校验（镜像头、芯片 ID、SHA256）通过后才切换启动项，
 *     中途断电/断网只会留下一个写坏的备份分区，重启永远回旧固件。
 *  3. 4KB 静态缓冲：httpd 任务栈默认只有 4KB，绝不能在栈上开大数组。
 *  4. 顺序擦写（OTA_WITH_SEQUENTIAL_WRITES）：边收边擦，不用先花十几秒
 *     把 2MB 分区整体擦完才开始收数据，手机端不会误以为「卡死」。
 *  5. 上传令牌（每次上电随机）：只为挡住手机同时连着热点与移动数据时，
 *     其它网页对本机发起的跨站 POST；热点本身无密码，它不是权限控制。
 */
#include "crid_ota.h"

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "CN_C-RID_OTA";

// flash 扇区大小，也是单次接收/写入的块大小
#define OTA_CHUNK        4096
// 小于这个体积的一定不是固件（正常镜像 600KB 以上），提前拒绝省得白擦分区
#define OTA_MIN_IMAGE    (32 * 1024)
// 连续 recv 超时次数上限：手机锁屏/切走时 socket 会一直超时，
// 无限重试会让 httpd 任务永远卡在这个请求上
#define OTA_MAX_TIMEOUTS 8

static char s_chunk[OTA_CHUNK];
static char s_token[9];   // 8 位十六进制 + '\0'

void crid_ota_init(void) {
    snprintf(s_token, sizeof(s_token), "%08x", (unsigned)esp_random());
}

/// 统一的 JSON 错误回包：页面直接把 err 显示出来，不用猜 HTTP 码
static esp_err_t ota_fail(httpd_req_t *req, const char *status, const char *err) {
    char buf[192];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":\"%s\"}", err);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

esp_err_t crid_ota_handler_info(httpd_req_t *req) {
    const esp_partition_t *run  = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *desc  = esp_app_get_description();

    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"ver\":\"%s\",\"part\":\"%s\",\"part_size\":%u,\"max\":%u,"
             "\"token\":\"%s\",\"ota\":%s}",
             (desc && desc->version[0]) ? desc->version : "?",
             run ? run->label : "?",
             (unsigned)(run ? run->size : 0),
             (unsigned)(next ? next->size : 0),
             s_token,
             next ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t crid_ota_handler_upload(httpd_req_t *req) {
    // ---- 1. 令牌 ----
    char tok[16] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Token", tok, sizeof(tok)) != ESP_OK ||
        strcmp(tok, s_token) != 0) {
        ESP_LOGW(TAG, "upload rejected: bad token");
        return ota_fail(req, "403 Forbidden", "上传令牌无效，请刷新页面后重试");
    }

    // ---- 2. 必须是裸二进制流（页面显式设置，排除浏览器表单误提交） ----
    char ct[48] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof(ct)) != ESP_OK ||
        strncmp(ct, "application/octet-stream", 24) != 0) {
        return ota_fail(req, "415 Unsupported Media Type", "只接受 application/octet-stream");
    }

    // ---- 3. 目标分区与容量 ----
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        return ota_fail(req, "500 Internal Server Error", "没有可用的 OTA 分区");
    }
    size_t total = req->content_len;
    if (total < OTA_MIN_IMAGE) {
        return ota_fail(req, "400 Bad Request", "文件过小，不是有效固件");
    }
    if (total > part->size) {
        return ota_fail(req, "400 Bad Request", "文件超过 OTA 分区容量");
    }

    // ---- 4. 开始写入 ----
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        // 回滚已开启时，本次运行固件还没被确认（PENDING_VERIFY）会走到这里
        return ota_fail(req, "500 Internal Server Error",
                        err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE
                            ? "当前固件尚未确认有效，请重启设备后再升级"
                            : "无法写入升级分区");
    }

    size_t writ = 0;
    int timeouts = 0;
    while (writ < total) {
        int want = (total - writ) > OTA_CHUNK ? OTA_CHUNK : (int)(total - writ);
        int got = httpd_req_recv(req, s_chunk, want);

        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > OTA_MAX_TIMEOUTS) {
                esp_ota_abort(handle);
                ESP_LOGE(TAG, "recv timeout x%d, aborted at %u/%u", timeouts, (unsigned)writ, (unsigned)total);
                return ota_fail(req, "408 Request Timeout", "传输超时，已取消升级，旧固件不受影响");
            }
            continue;
        }
        if (got <= 0) {
            esp_ota_abort(handle);
            ESP_LOGE(TAG, "recv failed at %u/%u", (unsigned)writ, (unsigned)total);
            return ESP_FAIL;   // socket 已断，回包也没人收
        }
        timeouts = 0;

        err = esp_ota_write(handle, s_chunk, got);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            return ota_fail(req, "400 Bad Request",
                            err == ESP_ERR_OTA_VALIDATE_FAILED
                                ? "文件不是本芯片的固件（镜像头不匹配）"
                                : "写入 flash 失败");
        }
        writ += got;
        if ((writ & 0xFFFF) < OTA_CHUNK) {
            ESP_LOGI(TAG, "written %u/%u KB", (unsigned)(writ / 1024), (unsigned)(total / 1024));
        }
    }

    // ---- 5. 校验并切换启动分区 ----
    err = esp_ota_end(handle);   // 校验 SHA256 / 芯片 ID，失败说明镜像有问题
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return ota_fail(req, "400 Bad Request",
                        err == ESP_ERR_OTA_VALIDATE_FAILED
                            ? "固件校验失败（芯片型号不匹配或文件损坏）"
                            : "固件校验失败");
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        return ota_fail(req, "500 Internal Server Error", "切换启动分区失败");
    }

    // ---- 6. 先回包再重启：客户端要拿到 ok 才知道升级成功 ----
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"size\":%u}", (unsigned)writ);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);

    ESP_LOGW(TAG, "OTA success: %u bytes -> %s, rebooting", (unsigned)writ, part->label);
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
    return ESP_OK;
}
