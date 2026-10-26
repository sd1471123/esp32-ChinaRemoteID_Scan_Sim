# ESP32-ChinaRemoteID_Scan_Sim

ESP32 无人机 Remote ID **广播（模拟）/ 接收（扫描）一体固件**，单程序双模式，网页在线配置。

- **发射模式**：模拟无人机 Remote ID 广播，支持 **GB 42590-2023（旧国标）/ GB 46750-2025（新国标）** 两档切换，同时兼容 ASTM F3411-22 与 ASD-STAN prEN 4709-002
- **接收模式**：Wi-Fi 混杂模式监听所在信道，同时解码 **GB 42590 / GB 46750 / ASTM** 三种协议的 RID 报文，网页实时展示周边无人机与飞手位置
- **零编译配置**：手机/电脑连接设备热点，浏览器访问 `http://192.168.4.1` 即可修改全部参数并持久化（NVS 断电保持）

已在 **ESP32-S3** 与 **ESP32-C5**（均为 N16R8，16MB Flash）上实测；其他 4MB 以上 Flash 的 ESP32 模组可自行调整分区表编译。

## 界面预览

连接设备热点后访问 `http://192.168.4.1`，网页顶部可切换发射 / 接收模式：

| 发射模式（GB 46750 新国标） | 接收模式（监测扫描） |
|---|---|
| ![发射模式](docs/webui_tx.png) | ![接收模式](docs/webui_rx.png) |

## 功能总览

### 发射模式（模拟器）

- **双档广播标准**：GB 42590（OpenDroneID 打包报文）、GB 46750（位图数据项报文，13 项必选），网页切换自动显隐对应配置项
- **稳定广播**：softAP 真实热点 + vendor IE 注入（OUI `FA:0B:BC`），硬件自动应答探测请求，手机 WLAN 列表稳定可见，无闪现/消失问题
- **在线参数配置**：UAS ID、机型名称、机型代码（RID 标准 16 类航空器类别码）、操作员 ID、GPS 基准坐标、高度、速度、运行状态、巡游参数、热点 SSID/信道/广播间隔，保存即时生效
- **大疆机型伪装预设**：内置 40 余款大疆主流机型（Mavic / Air / Mini / Avata / Neo / FPV / Matrice / Inspire / FlyCart 等），选型号自动生成符合真实格式的 20 位序列号并同步填入机型名称与机型代码
- **地图选点**：Leaflet + OpenStreetMap 点击/拖动选点自动填入坐标（WGS-84，与 RID 广播坐标系一致，无 GCJ-02 偏移）；支持独立设置飞手位置并与无人机位置联动；实时显示广播位置、**飞行轨迹（蓝线）**与巡游范围圈；无互联网自动降级离线网格拾取器
- **巡游路径模拟**：固定点或圆形巡游，半径/速度/圈数可调

### 接收模式（扫描器）

- **多协议解码**：ASTM F3411 / GB 42590 / GB 46750 报文全部解析（GB 46750 支持全部 21 个数据内容项）
- **实时监测页面**：卡片式列表展示周边 RID 信号——识别码、机型、位置、高度、速度、航向、协议类型、信号强度等；**在线 / 已离线分组显示**（10 秒无信号转入离线组，灰度置底，保留 30 分钟）；点击卡片可在地图上聚焦显示目标轨迹与飞手位置连线
- **双地图三态渲染**：联网时 Leaflet + OSM 地图；CDN 不可达或无互联网时自动降级为 canvas 离线网格底图（比例尺按目标包围盒自适应 1/2/5 档位）；完全无信号时显示空态提示
- **目标航迹**：每架无人机的历史位置绘制为地图轨迹线（在线实线、离线灰虚线），离线网格模式下同步绘制折线
- **飞手位置解析**：GB 46750 数据项 005/006/007 与 GB 42590 System 消息中的操作员坐标均可解析显示

### 网页与传输

- 响应式设计：电脑端（≥860px）双列卡片 + 大地图通栏，手机端单列自适应
- 内嵌页面与地图脚本以 **gzip 压缩**下发（约 30KB → 11KB），带禁缓存响应头
- 接收端地图脚本作为**独立资源按需懒加载**（`/rxmap.js`），与发射端地图零符号共享，单点故障不影响页面其余功能

## 使用方法

1. 烧录固件后，手机连接热点 `ESP32-CRID-XXXX`（SSID/密码可在配置中修改）；
2. 浏览器打开 `http://192.168.4.1`；
3. 网页顶部切换 **发射 / 接收** 模式；发射模式下可切换广播标准（GB 42590 / GB 46750）；
4. 修改 UAS ID、基准坐标（可地图选点）、高度、速度、巡游参数等，点击「保存并应用」即时生效；
5. 修改 SSID / 信道 / 广播间隔后，需重新连接热点；
6. 「恢复初始」按钮一键还原全部默认配置（保留消息计数器，防止接收端乱序）；
7. 接收模式下，监听信道跟随上方「Wi-Fi 信道」设置。

## 构建与烧录

### Windows 一键构建（推荐）

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

自动加载 ESP-IDF 环境、生成页面 gzip 资源、ccache 加速编译并输出耗时与命中率。

### 手动构建（Linux/macOS 或已配置好的 IDF 环境）

```bash
idf.py set-target esp32s3    # 或 esp32c5（自动叠加 sdkconfig.defaults.esp32c5）
idf.py build
```

> S3 与 C5 频繁切换编译互不干扰：两芯片各自的 flash 大小等差异项
> 已写入 `sdkconfig.defaults.esp32s3` / `sdkconfig.defaults.esp32c5`，
> `set-target` 时自动叠加生效（构建目录会全量重编一次属正常）。

### 烧录

开发板 USB 连接电脑（Windows 下串口通常为 `COM3`，按设备管理器实际端口修改）：

```bash
idf.py -p COM3 flash monitor    # 烧录并打开串口监视器（Ctrl+] 退出）
```

也可分开执行：`idf.py -p COM3 flash` 只烧录，`idf.py -p COM3 monitor` 只开监视器。

## 预编译固件下载

到 [Releases](https://github.com/sd1471123/esp32-ChinaRemoteID_Scan_Sim/releases) 页面按芯片下载对应 zip（CI 自动构建，均按 **16MB Flash** 编译）：

| 芯片 | 下载包 | bootloader 偏移 | 实测 |
|------|--------|-----------------|------|
| ESP32-S3 | `esp32-ChinaRemoteID_Scan_Sim-esp32s3.zip` | `0x0` | N16R8 ✓ |
| ESP32-C5 | `esp32-ChinaRemoteID_Scan_Sim-esp32c5.zip` | **`0x2000`（与 S3 不同）** | N16R8 ✓ |

解压后用 esptool 烧录（在 zip 目录内执行）：

```bash
# ESP32-S3
esptool.py --chip esp32s3 --baud 460800 write_flash \
  0x0 remoteid_scanner_esp32s3/bootloader/bootloader.bin \
  0x8000 remoteid_scanner_esp32s3/partition_table/partition-table.bin \
  0x10000 remoteid_scanner_esp32s3/remoteid_scanner.bin

# ESP32-C5（bootloader 地址是 0x2000，不是 0x0！）
esptool.py --chip esp32c5 --baud 460800 write_flash \
  0x2000 remoteid_scanner_esp32c5/bootloader/bootloader.bin \
  0x8000 remoteid_scanner_esp32c5/partition_table/partition-table.bin \
  0x10000 remoteid_scanner_esp32c5/remoteid_scanner.bin
```

也可使用 [flash download tool](https://www.espressif.com/en/support/download/other-tools) 图形界面，选择对应芯片型号与上表地址烧录。

## 项目结构

```
├── main/                         # 统一收发固件（唯一构建目标）
│   ├── crid-sim.c                # 主入口：设备模式应用与主任务分发
│   ├── crid_config.c/h           # 配置管理（NVS 持久化，含设备模式/广播标准）
│   ├── crid_wifi.c/h             # softAP 初始化 / 厂商 IE 注入（双槽位）
│   ├── crid_messages.c/h         # GB 42590 / ASTM 消息编码（Message Counter + Packed）
│   ├── crid_gb46750.c/h          # GB 46750 新国标位图报文构建
│   ├── crid_rx.c/h               # 接收端：混杂模式监听 + 多协议解码 + 飞手位置解析
│   ├── crid_patrol.c/h           # 巡游路径模拟
│   ├── crid_web.c/h              # 内置网页配置与接收监测（http://192.168.4.1）
│   ├── rxmap_page.js             # 接收端地图独立脚本（懒加载，无网自动降级离线网格）
│   └── page_gz.h                 # 自动生成的 gzip 页面资源（构建时生成，不入库）
├── components/opendroneid/       # OpenDroneID 官方库（编解码核心）
├── partition_table/              # 分区表
├── tools/                        # 构建与回归工具
│   ├── build.ps1                 # 统一构建入口（IDF 环境 + 页面 gzip + ccache）
│   ├── gen_gzpage.py             # 内嵌网页/地图脚本 gzip 资源生成
│   ├── check_page.py             # 页面静态断言（语法/结构/地图隔离）
│   ├── jsdom_test.js             # 整页真实 DOM 回归（加载/恢复/保存/切模式）
│   ├── rx_render_test.js         # 接收端渲染测试桩
│   ├── rxmap_test.js             # 接收端地图模块 21 项隔离测试
│   ├── extract_bin_page.py       # 固件 bin 内嵌页面核对
│   └── json_monitor.py           # 旧版接收器串口 JSON 监视器
├── .github/workflows/            # CI：push/tag 自动构建与发布 Release
├── CMakeLists.txt                # 根构建文件
└── sdkconfig                     # 工程配置
```

## 协议参数

| 参数 | 值 |
|------|-----|
| OUI | `FA:0B:BC`（ASTM / 国标统一） |
| Vendor Type | `0x0D` |
| IE ID | 221（Vendor Specific） |
| Wi-Fi 信道 | 6（2.437 GHz，可网页修改） |
| 广播间隔 | 1 Hz |
| 消息格式 | Message Pack（含 Message Counter） |

### 协议支持明细

| 标准 | OUI | 格式 | 解析方式 |
|------|-----|------|---------|
| ASTM F3411-22a | `FA:0B:BC` | ODID_MessagePack_encoded，Message Counter + Packed 消息 | `odid_message_process_pack()` |
| GB 42590-2023 | `FA:0B:BC` | 国标 Packed 格式，Message Counter + 3 字节管理信息 + 消息体 | 转为 ASTM 兼容头部后 `odid_message_process_pack()` |
| GB 46750-2025 | `FA:0B:BC` | Message Counter + 0xFF + 版本号 + 内容长度 + 数据标识(3B) + 数据内容(变长) | 自定义解析 21 个数据内容项 |

> 所有 Wi-Fi Beacon 标准统一使用 OUI `FA:0B:BC`、Vendor Type `0x0D`。
> GB 46750 使用 `data[1]==0xFF` 作为数据类型标识，数据标识固定 3 字节（按位指示字段存在性），
> 支持全部 21 个数据内容项（001-021）的解析。
> ASTM 与 GB 42590 在 Wire 格式上均以 `0xF1` 开头（ASTM 为位域编码
> `MessageType=0xF` + `ProtoVersion=1`，国标为独立 magic byte），
> 但语义上需要区分协议类型。

## 版本历史

### v1.1.0 — C5 支持 / 轨迹 / 在线离线分组

- **ESP32-C5 支持**：16MB Flash 自定义分区表（factory 1.5MB），S3 / C5 各自的
  `sdkconfig.defaults.esp32*` 随 `set-target` 自动叠加，双芯片切换编译互不干扰；两芯片均已实测
- **飞行轨迹**：发射端地图实时绘制广播位置轨迹（蓝线）；接收端为每架无人机绘制历史航迹（在线实线 / 离线灰虚线），离线网格模式同步绘制
- **在线 / 离线分组**：接收列表按信号活跃度分组（10 秒无信号转离线组、灰度置底、保留 30 分钟后清除）
- **界面优化**：发射模式双列布局改为左右等高（列尾卡片自动拉伸补齐）
- **精简**：删除发射端「双标准同发」模式，广播标准仅保留 GB 42590 / GB 46750 两档（NVS 中旧配置自动回落默认值）
- **修复**：CI 构建前自动生成页面 gzip 资源（page_gz.h 不入库导致的历史构建失败）

### v1.0.0 — 首个发布版本

本仓库首个独立版本，功能基线：

- **统一收发固件**：单程序双模式，网页在线切换发射 / 接收，无需分别烧录
- **发射**：双档广播标准（GB 42590 / GB 46750）；softAP + vendor IE 稳定广播；全套在线参数配置与 NVS 持久化；大疆机型伪装预设；地图选点与巡游模拟
- **接收**：多协议实时解码（ASTM / GB 42590 / GB 46750，含飞手位置）；卡片式监测列表 + 点击地图聚焦；双地图三态渲染（在线地图 / 离线网格 / 空态）
- **工程化**：页面 gzip 传输与禁缓存头；接收端地图独立资源懒加载架构；一键构建脚本与五套回归测试工具；GitHub Actions 自动构建发布

## 致谢与引用来源

本项目站在以下开源项目的肩膀上，在此致谢：

| 项目 | 引用方式 |
|------|---------|
| [luolitao/esp32-crid](https://github.com/luolitao/esp32-crid) | **本项目的上游基础**。原始的 RID 广播/接收思路、工程骨架与 Wi-Fi 射频层实现均源自该项目，本仓库在其基础上做了 softAP 广播重构、双国标支持与统一收发固件等大量修改 |
| [OpenDroneID](https://github.com/opendroneid/opendroneid)（`components/opendroneid/`） | **Remote ID 消息编解码核心库**。ASTM / GB 42590 打包报文的编码与解析均基于该官方库，沿用其原许可证（Apache-2.0） |
| [sxjack/uav_electronic_ids](https://github.com/sxjack/uav_electronic_ids) | OpenDroneID 官方 ESP32 参考实现。本项目 softAP + vendor IE 广播方案与其保持一致（`esp_wifi_set_vendor_ie()` 挂载 Beacon/Probe Response） |
| [TechExplorer-X/DroneScanner](https://github.com/TechExplorer-X/DroneScanner) | **大疆机型 RID 序列号前缀数据来源**。「大疆机型伪装预设」的机型前缀对照表（`1581F` 厂商前缀 + 第 6~8 位型号码）取自该项目的社区实测机型库，仅用于测试研究 |
| [Leaflet](https://leafletjs.com/)（v1.9.4） | 发射/接收两端网页地图渲染框架（CDN 引入，未修改分发） |
| [OpenStreetMap](https://www.openstreetmap.org/) | 地图瓦片图源（WGS-84，与 RID 广播坐标系一致，无 GCJ-02 偏移） |
| [ESP-IDF](https://github.com/espressif/esp-idf)（v6.x） | Espressif 官方物联网开发框架，本项目构建于其上 |

> 各项功能开发过程中的小改动不再逐条列出，完整改动见[提交历史](https://github.com/sd1471123/esp32-ChinaRemoteID_Scan_Sim/commits/main)。

## 免责声明

本项目广播的是**虚构的 Remote ID 信号**，仅用于协议研究、接收端开发验证与测试。请遵守当地法律法规，**切勿**在机场净空区等敏感区域当作真实无人机信号广播，切勿用于规避监管或干扰合法飞行活动。使用本软件产生的一切后果由使用者自行承担。

## 许可证

本项目基于 [MIT License](LICENSE) 开源。内置的 OpenDroneID 库沿用其原许可证（Apache-2.0）。
