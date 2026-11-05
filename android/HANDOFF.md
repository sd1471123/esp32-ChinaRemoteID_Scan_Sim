# C-RID 扫描器：Android 客户端 + 固件补丁 交接说明

交接包日期：2026-11-05
上游仓库：https://github.com/sd1471123/esp32-ChinaRemoteID_Scan_Sim

## 1. 包内内容

```
HANDOFF.md                          本文件
android/                            完整 Android 工程源码（不含 .gradle、app/build 等构建产物）
apk/app-debug.apk                   已编译的 debug 安装包（18 MB，包名 com.crid.app）
firmware/crid_web.c                 打完补丁的完整文件（main/crid_web.c，2.0 参考）
firmware/crid-web-80-api.patch      相对 main 分支的 unified diff，可直接 git apply
```

补丁只改 `main/crid_web.c` 一个文件；Android 工程是新增目录 `android/`。
APK 能直接安装，但蓝牙通道要固件补丁烧录后才会正常返回 200。

## 2. 背景与目标

- 固件是 ESP32 C-RID 扫描/模拟器，已带网页版（HTTPS 192.168.4.1）。
- 本次新增 Android 原生客户端（Kotlin + Jetpack Compose），目标是与网页版功能对齐：
  手机蓝牙（BLE）读实时数据；连设备热点时走 HTTPS 配置/写入。
- 约束：不改动原有业务接口；写操作仍只走热点 HTTPS；蓝牙只做只读查询。

## 3. 固件改动（必须烧录才生效）

### 3.1 根因

`main/crid_ble.c` 的 BLE 桥接用 `http://<AP_IP><path>`（80 端口）访问本机 httpd，且
`disable_auto_redirect = true`。而 `main/crid_web.c` 在 HTTPS 正常启动时，80 端口只注册了
一个 `/*` 通配处理器，把请求 302 跳到 https。结果桥接拿到 302，视为失败，回 `0x04` 错误码 4，
手机端表现为「蓝牙读取失败：设备返回非 200」。

### 3.2 改动内容（main/crid_web.c 的 crid_web_start 内）

1. HTTPS 分支的 `max_uri_handlers` 从固定 2 改为 `sizeof(uris)/sizeof(uris[0]) + 2`。
2. `max_open_sockets` 由 2 调为 3（桥接连接 + 跳转请求各占一条，留余量）。
3. HTTPS 正常时，先注册所有 `method == HTTP_GET && uri` 以 `/api/` 开头的处理器，
   再注册原来的 `/*` 302 跳转（httpd 按注册顺序匹配，具体路径优先）。
4. 启动日志改为「80 端口放行 GET /api，其余 302 跳转」。

安全边界：80 端口只放行 **GET /api/***，POST（配置、OTA、日志删除等）仍只在 HTTPS；
明文 API 仅在设备自身 AP 内可达。

### 3.3 构建与烧录

```bat
cd <固件工程目录>
idf.py build
```

两种生效方式：
- 网页 OTA（不用 USB）：连设备热点，打开 https://192.168.4.1 → 固件升级 → 上传 `build\*.bin`
- USB：`idf.py -p COMx flash monitor`

### 3.4 验证

- 开机日志出现「Web 已就绪: https://192.168.4.1（80 端口放行 GET /api，其余 302 跳转）」。
- 用 nRF Connect 连 `ESP32-CRID-*`，向 RX 特征写入 UTF-8 字符串 `/api/rx`，
  应收到 0x01/0x02/0x03 数据帧，而不是 0x04 04。
- 手机 APP 连接后，监控页应在 1 秒内出现实时数据，不再提示「设备返回非 200」。
- 浏览器直接输入 `http://192.168.4.1` 仍应 302 跳到 https。

## 4. Android 客户端

### 4.1 技术栈

| 项 | 版本 |
|---|---|
| Kotlin | 2.1.20 |
| AGP / Gradle | 8.7.3 / 8.14.6（wrapper 已带） |
| compileSdk / targetSdk / minSdk | 35 / 35 / 26 |
| JDK | 17 |
| Compose | Material3，BOM 2024.12.01 |
| 网络 | OkHttp 4.12.0（自签名证书 trust-all，仅用于 192.168.4.1） |
| 地图 | osmdroid 6.1.20 |

### 4.2 功能

- 监控：BLE 连接卡（扫描/连接/断开/打开 Wi-Fi 设置）、统计（在线目标/RID 帧/总帧/解析成功/队列溢出/设备时间）、
  目标列表（按在线与 RSSI 排序）、轨迹详情、飞手位置、实名信息、高德/百度导航。
- 地图：osmdroid 实时打点，无人机与飞手不同图标，自动全览，在线计数。
- 轨迹：历史记录开关（GET /api/log/enable）、条数/占用/目标数/时间范围、CSV 导出与分享、
  清空、按目标删除、轨迹回放（30 条/页 × 最多 10 页，1x/2x/4x，进度滑杆）。
- 配置：读取/修改/保存全部参数（角色、频段、RID 标准、信道、间隔、TX 身份与飞行参数、巡游参数）、
  授时、恢复出厂、OTA（读取固件信息 + 上传 .bin 带 X-OTA-Token 与进度）。
- 自动授时：连上设备（BLE 或热点）即用手机系统时间校准设备时钟，之后每 10 分钟补一次；
  设备未校准（`/api/status` 的 `tsync == 0`，例如中途重启）时下一次轮询自动补授时。
  见 4.3 的节流说明与 `DeviceRepository.autoSyncTime()`。

### 4.3 双通道规则

- 读（GET）：BLE 已连接时全部走 BLE；否则走热点 HTTPS；两者都没有时每 5 秒探测热点。
- 写（`/api/config`、`/api/reset`）：**蓝牙和热点都行**。BLE 侧支持带请求体的 POST
  帧，设备收到后自己用 HTTPS 打回本机 443（见 4.4），手机不必连热点。
- OTA 上传仍需连热点：1.3 MB 级的固件包走蓝牙分片太慢。
- 轨迹操作走蓝牙也可以：固件为 `/api/log/del`、`/api/log/clear`、`/api/log/export` 开了 GET 变体
  （`/api/log/del?mac=…`、`/api/log/clear`、`/api/log/export[?mac=…]`），因为桥接只转发 GET。
  导出回包大时蓝牙很慢（满盘几 MB），App 侧把蓝牙超时放宽到 90 秒，界面仍建议大文件连热点。
- 授时同理：走 `GET /api/time?ms=<毫秒>`，没有蓝牙时才用 `POST /api/time`。自动授时的节流：
  真实请求间隔 ≥ 8 秒、连续失败 3 次即静默（换通道或重连重置）、成功后 10 分钟内不重复发。
- 轮询节奏：`/api/rx` 1s、`/api/status` 2s、`/api/log/info` 6s。

### 4.4 BLE 协议（务必保持）

- 扫描名前缀：`ESP32-CRID-`
- Service：`6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- 写（RX）：`6e400002-b5a3-f393-e0a9-e50e24dcca9e`（UTF-8 路径，或 POST 帧）
- 通知（TX）：`6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- 回包帧：`0x01` + 4 字节大端总长（开始）、`0x02` + 分片（数据）、`0x03`（结束）、
  `0x04` + 1 字节错误码（1 路径非法 / 2 客户端失败 / 3 本地 HTTP 打开失败 / 4 非 200 / 5 设备内存不足）
- **手机→设备的请求有两种形态**（靠首字节区分，老客户端不受影响）：
  - 首字节是 `/`（0x2F）：整包就是一条 GET 路径，长度 ≤ 94 字符，**不能分片**
  - 首字节 `0x12`：POST 头帧 —— `0x12 | 路径长度(1) | 请求体长度(2, 小端) | 路径 | 请求体开头`
  - 首字节 `0x13`：POST 续帧 —— `0x13 | 请求体后续`；设备收满「请求体长度」字节后执行
  - POST 上限：路径 95 字节、请求体 1536 字节；续帧不可重发（重发会让同一段写入两遍）
- 桥接出口：GET 走 `http://192.168.4.1`（80 端口只放行 GET `/api/` 前缀），
  POST 走 `https://192.168.4.1`（443，设备自签证书不校验）——这样 80 端口仍然不承担写操作
- 请求串行，GET 8 秒 / POST 15 秒超时；连接流程必须串行：发现服务 → 协商 MTU →
  开通知并等 CCCD 写入成功 → 才置为已连接。

### 4.5 关键文件

```
android/app/src/main/java/com/crid/app/data/Models.kt            数据模型（字段对齐固件 JSON）
android/app/src/main/java/com/crid/app/data/HttpApi.kt           HTTPS 客户端与全部接口
android/app/src/main/java/com/crid/app/data/BleClient.kt         BLE 扫描/连接/GATT/帧重组/串行队列
android/app/src/main/java/com/crid/app/data/DeviceRepository.kt  状态、双通道、轮询、写操作
android/app/src/main/java/com/crid/app/ui/*.kt                   监控/地图/轨迹/配置四个页面与通用组件
android/app/src/main/AndroidManifest.xml                         蓝牙、定位（≤API30）、网络权限
```

### 4.6 构建

```bat
cd android
gradlew.bat assembleDebug
```
产物：`android/app/build/outputs/apk/debug/app-debug.apk`。

依赖 JDK 17、SDK Platform 35、Build-Tools 35，首次同步需联网拉 AGP/Compose 依赖。
`gradle.properties` 已按 4G 内存环境调成 `-Xmx1536m` + Kotlin 进程内编译；内存充足的本机可自行调大。

## 5. 已修复的踩坑记录

1. **蓝牙读写全失败**：原实现按 MTU 分片发送路径，固件把每片当独立请求；且 `requestMtu` 与 CCCD 写入并发，
   GATT 栈拒绝后续操作。已改为：单次写完整条路径、连接流程严格串行、带响应写入失败后重试再降级。
2. **设备返回非 200**：即本文第 3 节固件 302 问题，APP 侧无法绕过。
3. 序列化 `decodeFromString` 导入、`deleteLog/clearLog` 回调默认值、OTA 进度重置、注释中 `/api/*` 导致
   Kotlin 块注释未闭合等编译问题均已处理。

## 6. 当前 Git 状态

- 远端 `main` 仍是 `572dce8`（未合入本次改动）。
- 工作区改动曾提交在 `trae/agent-ReJBAF`（`00cf1a0`）：author 是 `sd1471123 <admin@arara.top>`，
  committer 是 `trae-solo-agent`（IDE 自动快照用的环境身份）。该分支的远端已被删除，
  但环境后台可能再次创建/推送同名快照分支，最终以 GitHub 上是否可见为准。
- 若希望 **author 与 committer 都是本人**，提交前显式设置：
  ```bat
  set GIT_AUTHOR_NAME=sd1471123
  set GIT_AUTHOR_EMAIL=admin@arara.top
  set GIT_COMMITTER_NAME=sd1471123
  set GIT_COMMITTER_EMAIL=admin@arara.top
  ```
  或 `git commit --author="sd1471123 <admin@arara.top>"` 并确保不带 `GIT_COMMITTER_*` 覆盖。

## 7. 后续待办

- 烧录固件后真机验证 BLE 通道（重点：/api/rx 返回 200）。
- 若仍报蓝牙错误，采集提示里的 `status / MTU / props` 三个值再定位。
- 可选增强：离线底图（设备热点通常无外网，osmdroid 瓦片可能空白，标记与折线不受影响）、
  README/构建说明、release 签名、配置项分组折叠。

## 8. 快速验收清单

- [ ] `idf.py build` 通过，设备启动日志含「80 端口放行 GET /api」
- [ ] 浏览器 `http://192.168.4.1` 仍 302 跳 https
- [ ] APP 蓝牙连接后监控页 1 秒内有数据
- [ ] 断蓝牙、连热点后数据继续（HTTPS 通道）
- [ ] 配置页保存成功（热点通道）
- [ ] 轨迹页可导出 CSV、可回放
- [ ] OTA 页可读取版本并上传 bin
