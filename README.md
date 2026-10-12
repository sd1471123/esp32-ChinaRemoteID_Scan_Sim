# ESP32 C-RID (Remote ID Scanner & Simulator)

基于 ESP-IDF 6.0.1 的无人机 Remote ID 收发系统，遵循 ASTM F3411-22 / ASD-STAN prEN 4709-002 标准。主要部署在 ESP32-S3，兼容 ESP32-C3 等模组。

> 本仓库基于 [luolitao/esp32-crid](https://github.com/luolitao/esp32-crid) 修改，感谢原作者的开源工作。

## 本仓库的修改（相对上游）

发射器（main_tx）修复了"RID 信号在手机上出现几秒后消失、几分钟后又出现"的问题，并增强了接收稳定性：

1. **改用 softAP 模式广播**：原实现为 STA + 混杂模式下用 `esp_wifi_80211_tx` 手工发送 Beacon 帧，不应答探测请求（Probe Request），导致手机扫描时频繁漏抓。现改为 `WIFI_MODE_AP` 真实热点，硬件自动广播 Beacon 并应答探测请求，手机 WLAN 列表中稳定可见。该做法与 OpenDroneID 官方 ESP32 参考实现（sxjack/uav_electronic_ids）一致。
2. **RID 数据通过厂商 IE 注入**：使用 `esp_wifi_set_vendor_ie()` 将 RID 数据（OUI FA:0B:BC / Type 0x0D / Message Counter / 0xF1 打包报文，格式与上游完全一致）同时挂到 Beacon 和 Probe Response。每 `DEFAULT_BEACON_INTERVAL_MS` 更新一次 IE。
3. **修复重复帧过滤问题**：原 Beacon 帧 802.11 序列号恒为 0 且 `en_sys_seq=false`，接收端会按 802.11 协议将后续帧视为重传丢弃；现已修复。
4. **关闭 Wi-Fi 省电**：初始化时增加 `esp_wifi_set_ps(WIFI_PS_NONE)`。
5. **softAP vendor IE 更新**：修复 IDF 不允许直接覆盖已设置 IE 槽位的问题（先清除再设置）。
6. **网页配置（新增）**：手机连接设备热点后，浏览器访问 `http://192.168.4.1` 即可在线修改无人机 UAS ID / 机型名称 / 操作员 ID、GPS 基准坐标、高度、速度、运行状态、巡游参数，以及热点 SSID / 信道 / 广播间隔。保存即时生效并写入 NVS 断电保持，无需重新编译烧录。
7. **地图选点（v2.0 新增）**：配置页面内置地图（Leaflet + OpenStreetMap，WGS-84 图源与 RID 广播坐标系一致），点击/拖动选点自动填入基准坐标；显示巡游范围圈与实时广播位置（2 秒刷新）。手机无互联网时自动降级为离线网格拾取器。
8. **恢复出厂（v2.0 新增）**：网页一键恢复全部默认设定（保留消息计数器），写入 NVS。

> 注意：本模拟器广播的是虚构 ID，仅用于测试与开发验证，请勿在机场等敏感区域当真实无人机信号广播。

## 版本历史

- **v2.0.0**：发射器网页配置（在线改参数 / NVS 持久化）、地图选点（在线地图 + 离线网格双模式）、实时广播位置显示、一键恢复出厂。
- **v1.0.0**：softAP 稳定广播修复版（见上方"本仓库的修改"第 1~5 条）。

## 功能

- **接收器 (Scanner)**：Wi-Fi 混杂模式抓包，使用 OpenDroneID 库多协议解析 ASTM F3411-22a / ASD-STAN prEN 4709-002 / GB 42590-2023 / GB 46750-2023 标准的 Remote ID 信号。所有输出以 JSON 格式通过串口输出，每行一条完整的 JSON 对象。支持双端口输出：UAV 数据走 UART1（GPIO17），调试信息走 USB CDC。
- **发射器 (Simulator)**：模拟无人机广播 Beacon 帧（巡游路径），Vendor IE 包含 Message Counter + Packed 消息。
- **网页配置**：发射器为可连接热点（DHCP），手机浏览器访问 `http://192.168.4.1` 在线修改全部仿真参数，配置持久化于 NVS。

### 网页配置使用方法

1. 烧录发射器固件后，手机连接热点 `ESP32-CRID-XXXX`；
2. 浏览器打开 `http://192.168.4.1`；
3. 修改 UAS ID、基准坐标（可地图选点）、高度、速度、巡游参数等，点击"保存并应用"即时生效；
4. 修改 SSID / 信道 / 广播间隔后，需重新连接热点；
5. "恢复初始"按钮一键还原全部默认配置。

## 构建

```bash
# 接收器（默认）
idf.py -DMAIN_DIR=main_rx set-target esp32s3
idf.py build

# 发射器
idf.py -DMAIN_DIR=main_tx set-target esp32s3
idf.py build
```

> 注意：`set-target` 会清空 build 目录重新配置。切换 main_rx / main_tx 时务必重新执行 `set-target`（带上 `-DMAIN_DIR`），不能只跑 `idf.py build`。

## 烧录与监视

编译完成后，将开发板通过 USB 连接电脑（Windows 下串口通常为 `COM3`，按实际设备管理器里的端口号修改）：

```bash
# 烧录并打开串口监视器（Ctrl+] 退出监视器）
idf.py -p COM3 flash monitor

# 发射器烧录后：手机连接热点 ESP32-CRID-XXXX，浏览器访问 http://192.168.4.1 配置
# 接收器烧录后：串口输出 JSON 格式的 Remote ID 解析结果，可用 tools/ 上位机查看
```

也可以分开执行：`idf.py -p COM3 flash` 只烧录，`idf.py -p COM3 monitor` 只开监视器。

## 项目结构

```
├── main_rx/                      # 接收器
│   ├── crid_scan_main.c          # 主入口：启动 sniffer / parser / monitor 任务
│   ├── crid_sniffer.c/h          # Wi-Fi 混杂模式抓包，ISR 安全队列
│   ├── crid_parser.c/h           # opendroneid 库解码（Message Pack / 单消息）
│   ├── crid_tracker.c/h          # 无人机追踪表（线程安全，超时清理）
│   ├── crid_display.c/h          # 精简输出：新 UAV 1 行 + 每分钟状态摘要
│   ├── crid_json.c/h              # JSON 格式化输出（所有信息统一 JSON 格式）
│   └── crid_rx_types.h           # 接收端类型定义、OUI、配置常量
├── main_tx/                      # 发射器
│   ├── crid-sim.c                # 主入口（softAP + 厂商 IE 广播任务）
│   ├── crid_wifi.c/h             # softAP 初始化 / 厂商 IE 注入（保留 raw 帧发送）
│   ├── crid_web.c/h              # 内置网页配置服务器（http://192.168.4.1）
│   ├── crid_messages.c/h         # 消息编码（Message Counter + Packed + 厂商 IE 构建）
│   ├── crid_config.c/h           # 配置管理（坐标/速度/ID 等）
│   └── crid_patrol.c/h           # 巡游路径模拟
├── components/opendroneid/       # OpenDroneID 官方库（解码核心）
├── partition_table/              # 分区表
├── tools/                        # 上位机工具
│   └── json_monitor.py           # 串口 JSON 监视器（实时展示 + 退出摘要）
├── CMakeLists.txt                # 根构建文件（通过 MAIN_DIR 切换目标）
├── sdkconfig.defaults            # 默认配置覆盖
└── dependencies.lock             # 依赖锁定
```

## 协议参数

| 参数 | 值 |
|------|-----|
| OUI | `FA:0B:BC`（ASTM / 国标统一） |
| Vendor Type | `0x0D` |
| Wi-Fi 信道 | 6（2.437 GHz） |
| 广播间隔 | 1 Hz |
| IE ID | 221（Vendor Specific） |
| 消息格式 | Message Pack（含 Message Counter） |

## 接收端输出（JSON 格式，双端口）

所有输出以 JSON 格式发送，每行一条完整的 JSON 对象。

### 端口分配

| 端口 | 物理接口 | 内容 |
|------|---------|------|
| **数据端口** | UART1（GPIO17 TX, 115200 baud）| UAV 解析数据（`uav_discovery` / `uav_update` / `uav_status` / `uav_timeout` / `status`） |
| **调试端口** | USB CDC（stdout）| 启动信息、调试、告警、错误、解码诊断（`startup` / `debug` / `warning` / `error` / `decode_fail`） |

> 数据端口同时也会输出到 USB CDC，方便开发调试。上位机可以只连接 UART1 接收纯净的 UAV 数据流。

### 事件类型

| evt | 输出端口 | 说明 | 触发时机 |
|-----|---------|------|---------|
| `startup` | 调试 | 系统启动信息 | 启动时（横幅 + 详细参数） |
| `status` | 数据 | 定期状态统计 | 每 60 秒（抓包速率、活跃 UAV 数等） |
| `uav_discovery` | 数据 | 新 UAV 发现 | 首次收到某 MAC 的 RID 信号 |
| `uav_update` | 数据 | UAV 解析数据更新 | 每次解码成功后（完整字段） |
| `uav_status` | 数据 | UAV 活跃状态 | 每 60 秒（含 age_ms） |
| `uav_timeout` | 数据 | UAV 超时移除 | 5 分钟无信号后清理 |
| `uav_detail` | 数据 | UAV 完整详情 | 按需调用（所有 Basic ID、Auth 等） |
| `warning` | 调试 | 告警 | 追踪表满、信道设置失败等 |
| `error` | 调试 | 错误 | 初始化失败、任务创建失败等 |
| `debug` | 调试 | 调试信息 | 任务启动确认、Wi-Fi 模式确认等 |
| `decode_fail` | 调试 | 解码失败诊断 | 每 32 次失败输出一次（含原始字节） |

### UAV 数据字段

每次 `uav_update` 包含完整的无人机 Remote ID 数据：

```json
{
  "evt": "uav_update",
  "ts": 12345,
  "mac": "AA:BB:CC:DD:EE:FF",
  "rssi": -45,
  "channel": 6,
  "transport": "Wi-Fi Beacon",
  "protocol": "ASTM F3411",
  "msg_count": 10,
  "basic_id": {
    "id_type": "serial_number",
    "ua_type": "helicopter_or_multirotor",
    "uas_id": "SN12345678"
  },
  "location": {
    "status": "airborne",
    "latitude": 22.1234567,
    "longitude": 113.1234567,
    "alt_baro": 120.5,
    "alt_geo": 125.3,
    "height": 100.0,
    "height_ref": "over_takeoff",
    "direction": 45.0,
    "speed_h": 5.50,
    "speed_v": 0.00,
    "acc_h": "...",
    "acc_v": "...",
    "acc_baro": "...",
    "acc_speed": "...",
    "timestamp": 1234.5,
    "ts_acc": 1
  },
  "system": { "operator_loc_type": "takeoff", ... },
  "self_id": { "type": "text", "desc": "Drone #1" },
  "operator_id": { "type": 1, "id": "OP12345" },
  "auth": []
}
```

- 所有枚举值使用 snake_case 字符串（如 `"serial_number"`、`"airborne"`），便于下游解析
- 未获取到的字段为 `null`
- 字符串字段经过 JSON 转义，安全可解析

## 上位机监视工具

`tools/json_monitor.py` 从串口读取 ESP32 的 JSON 输出并实时展示。

```bash
# 安装依赖
pip install pyserial

# 自动检测串口，默认仅显示 UAV 数据
python3 tools/json_monitor.py

# 指定串口和模式
python3 tools/json_monitor.py -p /dev/tty.usbserial-A5069RR4 -m data
python3 tools/json_monitor.py -m debug      # 仅调试信息（启动/告警/解码失败）
python3 tools/json_monitor.py -m static     # 仅静态消息（basic_id/self_id/operator_id/system/auth，自动去重）
python3 tools/json_monitor.py -m all        # 所有事件

# 退出时不显示摘要
python3 tools/json_monitor.py --no-summary
```

### 显示模式

| 模式 | 说明 |
|------|------|
| `data`（默认） | UAV 解析数据：discovery / update / status / timeout |
| `debug` | 调试信息：startup / warning / error / debug / decode_fail |
| `static` | 仅无人机静态消息（basic_id、self_id、operator_id、system、auth），每个 MAC 每种字段只显示一次 |
| `all` | 全部事件 |

### 退出摘要

按 `Ctrl+C` 退出时自动打印会话摘要，包括运行时长、发现/活跃 UAV 数量、解码失败计数、全局状态快照、每个 UAV 的详细信息和活跃状态。可用 `--no-summary` 跳过。

## 协议支持

使用 OpenDroneID 库实现多协议解析：

| 标准 | OUI | 格式 | 解析方式 |
|------|-----|------|---------|
| ASTM F3411-22a | `FA:0B:BC` | ODID_MessagePack_encoded，Message Counter + Packed 消息 | `odid_message_process_pack()` |
| GB 42590-2023 | `FA:0B:BC` | 国标 Packed 格式，Message Counter + 3 字节管理信息 + 消息体 | 转为 ASTM 兼容头部后 `odid_message_process_pack()` |
| GB 46750-2023 | `FA:0B:BC` | Message Counter + 0xFF + 版本号 + 内容长度 + 数据标识(3B) + 数据内容(变长) | 自定义解析 21 个数据内容项 |

> 所有 Wi-Fi Beacon 标准统一使用 OUI `FA:0B:BC`、Vendor Type `0x0D`。
> GB 46750 使用 `data[1]==0xFF` 作为数据类型标识，数据标识固定 3 字节（按位指示字段存在性），
> 支持全部 21 个数据内容项（001-021）的解析。
> ASTM 与 GB 42590 在 Wire 格式上均以 `0xF1` 开头（ASTM 为位域编码
> `MessageType=0xF` + `ProtoVersion=1`，国标为独立 magic byte），
> 但语义上需要区分协议类型。
