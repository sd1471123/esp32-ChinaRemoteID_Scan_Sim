/**
 * @file crid_ble.h
 * @brief 蓝牙数据通道：网页用 Web Bluetooth 读设备的 REST 查询接口
 *
 * 场景：手机连着本机热点时 Wi-Fi 被占满，地图瓦片拉不下来。这里用 BLE 把
 * 设备的查询接口搬到蓝牙上，网页连上蓝牙后就可以断开 Wi-Fi、用流量看地图。
 *
 * 实现上没有为蓝牙重写一套接口——BLE 收到请求后由设备内部用 HTTP 客户端访问
 * 自己的 httpd（192.168.4.1），把响应原样分片回传。这样 /api/rx、/api/status、
 * /api/log/… 等所有 GET 接口天然都能用，不需要维护两套 JSON 生成代码。
 *
 * 协议（Nordic UART 风格的 128 位 UUID，nRF Connect 等通用工具也能直接用）：
 *   写入 RX 特征：请求路径的 UTF-8 字符串，如 /api/rx（长度 <= 94）
 *   订阅 TX 特征：响应按帧回传，每帧 1 字节类型 + 数据
 *     0x01 + 4 字节大端总长度（开始，长度未知时为 0）
 *     0x02 + 数据（可多帧，每帧 <= MTU-3）
 *     0x03（结束，无数据）
 *     0x04 + 1 字节错误码（结束，无有效数据）
 *
 * 网页侧解析见 crid_web.c 里的 bleFetch()，两者必须一起改。
 */

#ifndef CRID_BLE_H
#define CRID_BLE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 服务与特征 UUID（与网页里的常量保持一致） */
#define CRID_BLE_SVC_UUID   "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CRID_BLE_RX_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CRID_BLE_TX_UUID    "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

/**
 * @brief 启动蓝牙数据通道（NimBLE 外设 + 广播）
 *
 * 必须在 Wi-Fi / httpd 起来之后调用：蓝牙桥接要靠本机 httpd 取数据。
 * 失败只是没有蓝牙通道，不影响 Wi-Fi 下的正常使用，调用方不必中断启动。
 */
esp_err_t crid_ble_init(void);

/** @brief 当前是否有手机连着蓝牙 */
bool crid_ble_connected(void);

#ifdef __cplusplus
}
#endif

#endif // CRID_BLE_H
