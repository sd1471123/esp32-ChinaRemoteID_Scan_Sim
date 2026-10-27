# ESP32-ChinaRemoteID_Scan_Sim

ESP32 无人机 Remote ID **广播（模拟）/ 接收（扫描）一体固件**，单程序双模式，网页在线配置。

- **发射模式**：模拟无人机 Remote ID 广播，支持 **GB 42590-2023（旧国标）/ GB 46750-2025（新国标）** 两档切换，同时兼容 ASTM F3411-22 与 ASD-STAN prEN 4709-002
- **接收模式**：Wi-Fi 混杂模式监听所在信道，同时解码 **GB 42590 / GB 46750 / ASTM** 三种协议的 RID 报文，网页实时展示周边无人机与飞手位置
- **单射频二选一**：一条射频链路同一时刻只能发或只能收，网页用「接收 / 发射」拨动开关切换，不会出现双开互相抢空口；热点本身常开，随时可连进来改参数
- **双频段（ESP32-C5）**：2.4 GHz / 5.8 GHz（国标 5.8G 段 5725–5829 MHz，信道 149–165）可网页切换；**编译期按芯片裁剪**——S3 等 2.4G-only 芯片编出的固件里射频层 5G 代码整体消失（连 `esp_wifi_set_band_mode` 符号都不存在），页面也自动隐藏 5.8G 选项
- **零编译配置**：手机/电脑连接设备热点，浏览器访问 `http://192.168.4.1` 即可修改全部参数并持久化（NVS 断电保持）

已在 **ESP32-S3** 与 **ESP32-C5**（均为 N16R8，16MB Flash）上实测；其他 4MB 以上 Flash 的 ESP32 模组可自行调整分区表编译。

## 界面预览

连接设备热点后访问 `http://192.168.4.1`，网页顶部可切换发射 / 接收模式（下图为 ESP32-C5 固件，
S3 固件不显示 5.8 GHz 选项）：

| 发射模式（C5 · 5.8 GHz 频段 · GB 46750 新国标） | 接收模式（监测扫描） |
|---|---|
| ![发射模式](docs/webui_tx.png) | ![接收模式](docs/webui_rx.png) |

## 功能总览

### 发射模式（模拟器）

- **双档广播标准**：GB 42590（OpenDroneID 打包报文）、GB 46750（位图数据项报文，13 项必选），网页切换自动显隐对应配置项
- **稳定广播**：softAP 真实热点 + vendor IE 注入（OUI `FA:0B:BC`），硬件自动应答探测请求，手机 WLAN 列表稳定可见，无闪现/消失问题
- **双频段广播（C5）**：2.4 GHz / 5.8 GHz 二选一，切换后自动重建热点；5.8G 锁 149–165（默认 149，符合 GB 42590 A.1.3.1.3），并显式设置国家码 `CN`（默认 world-safe 会裁掉大部分 5G 信道）与 20 MHz 带宽
- **在线参数配置**：UAS ID、机型名称、机型代码（RID 标准 16 类航空器类别码）、操作员 ID、GPS 基准坐标、高度、速度、运行状态、巡游参数、热点 SSID/信道/广播间隔，保存即时生效
- **大疆机型伪装预设**：内置 40 余款大疆主流机型（Mavic / Air / Mini / Avata / Neo / FPV / Matrice / Inspire / FlyCart 等），选型号自动生成符合真实格式的 20 位序列号并同步填入机型名称与机型代码
- **地图选点**：Leaflet + OpenStreetMap 点击/拖动选点自动填入坐标（WGS-84，与 RID 广播坐标系一致，无 GCJ-02 偏移）；支持独立设置飞手位置并与无人机位置联动；实时显示广播位置、**飞行轨迹（蓝线）**与巡游范围圈；无互联网自动降级离线网格拾取器
- **巡游路径模拟**：固定点或圆形巡游，半径/速度/圈数可调

### 接收模式（扫描器）

- **多协议解码**：ASTM F3411 / GB 42590 / GB 46750 报文全部解析（GB 46750 支持全部 21 个数据内容项）
- **实时监测页面**：卡片式列表展示周边 RID 信号——识别码、机型、位置、高度、速度、航向、协议类型、信号强度等；**在线 / 已离线分组显示**（10 秒无信号转入离线组，灰度置底，保留 30 分钟）；每张卡片可展开「详情」查看该目标的**全部数据项**，并支持**一键展开 / 折叠全部**；点击卡片可在地图上聚焦显示目标轨迹与飞手位置连线
- **双地图三态渲染**：联网时 Leaflet + OSM 地图；CDN 不可达或无互联网时自动降级为 canvas 离线网格底图（比例尺按目标包围盒自适应 1/2/5 档位）；完全无信号时显示空态提示
- **目标航迹**：每架无人机的历史位置绘制为地图轨迹线（在线实线、离线灰虚线），离线网格模式下同步绘制折线
- **飞手位置解析**：GB 46750 数据项 005/006/007 与 GB 42590 System 消息中的操作员坐标均可解析显示
- **频段跟随**：监听频段与信道跟随网页设置（C5 可切 5.8 GHz 监听），换信道/换频段前会先停掉混杂监听，避免驱动报错

### 网页与传输

- 响应式设计：电脑端（≥860px）双列卡片 + 大地图通栏，手机端单列自适应
- 内嵌页面与地图脚本以 **gzip 压缩**下发（约 30KB → 11KB），带禁缓存响应头
- 接收端地图脚本作为**独立资源按需懒加载**（`/rxmap.js`），与发射端地图零符号共享，单点故障不影响页面其余功能

## 使用方法

1. 烧录固件后，手机连接热点 `ESP32-CRID-XXXX`（SSID/密码可在配置中修改）；
2. 浏览器打开 `http://192.168.4.1`；
3. 网页顶部拨动开关切换 **接收 / 发射**（单射频二选一，同一时刻只有一边生效）；发射模式下可切换广播标准（GB 42590 / GB 46750），ESP32-C5 还可切换 2.4 GHz / 5.8 GHz 频段
4. 修改 UAS ID、基准坐标（可地图选点）、高度、速度、巡游参数等，点击「保存并应用」即时生效；
5. 修改 SSID / 信道 / 广播间隔 / 频段后热点会重建，手机需重新连接（页面会提示，先回包再重建，不会报错）；
6. 「恢复初始」按钮一键还原全部默认配置（保留消息计数器，防止接收端乱序）；
7. 接收模式下，监听信道跟随「Wi-Fi 信道」设置（5.8 GHz 固定 149–165，默认 149）。

## 构建与烧录

### Windows 一键构建（推荐）

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

自动加载 ESP-IDF 环境、生成页面 gzip 资源、ccache 加速编译并输出耗时与命中率。

烧录时把 `idf.py` 参数原样接在后面：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -p COM3 flash
```

> **两块板同时插在电脑上时务必显式带 `-p COMx`**：S3 与 C5 的 USB VID/PID 相同
> （`303A:1001`，原生 USB-Serial-JTAG），设备管理器里名字也一样，只能靠端口号区分。
> 不指定端口时 esptool 会自动挑一个，很可能挑到另一块芯片并报
> `This chip is ESP32-C5, not ESP32-S3`。
> 切换过芯片后需要先删掉 `build/` 再 `set-target`，否则会用上一款芯片的产物烧录。

### 手动构建（Linux/macOS 或已配置好的 IDF 环境）

```bash
idf.py set-target esp32s3    # 或 esp32c5（自动叠加 sdkconfig.defaults.esp32c5）
idf.py build
```

> S3 与 C5 频繁切换编译互不干扰：两芯片各自的 flash 大小等差异项
> 已写入 `sdkconfig.defaults.esp32s3` / `sdkconfig.defaults.esp32c5`，
> `set-target` 时自动叠加生效（构建目录会全量重编一次属正常）。

> **5.8 GHz 只存在于 C5 固件**：频段能力由 `CONFIG_IDF_TARGET_ESP32C5` 在编译期决定
> （`CRID_HAS_5G_BAND`），`idf.py set-target esp32s3` 编出的固件里 5G 射频代码被整体裁掉、
> 页面隐藏 5.8G 选项。想让 S3 也能用 5G 需要换芯片，改配置无效——S3 的射频本身只支持 2.4 GHz。

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
│   ├── crid_wifi.c/h             # softAP 初始化 / 频段切换（2.4G/5.8G）/ 厂商 IE 注入（双槽位）
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
| Vendor Type | `0x0D`（ASTM / GB 42590），`0xF0`（GB 46750） |
| IE ID | 221（Vendor Specific） |
| 频段 | 2.4 GHz（2400–2476 MHz）/ 5.8 GHz（5725–5829 MHz，**仅 ESP32-C5**） |
| Wi-Fi 信道 | 2.4 GHz：6（2.437 GHz）；5.8 GHz：149（5.745 GHz）——均可网页修改 |
| 广播间隔 | 1 Hz |
| 消息格式 | Message Pack（含 Message Counter） |

### 协议支持明细

| 标准 | OUI / Vendor Type | 格式 | 解析方式 |
|------|------|---------|---------|
| ASTM F3411-22a | `FA:0B:BC` / `0x0D` | ODID_MessagePack_encoded，Message Counter + Packed 消息 | `odid_message_process_pack()` |
| GB 42590-2023 | `FA:0B:BC` / `0x0D` | 国标 Packed 格式，Message Counter + 3 字节管理信息 + 消息体 | 转为 ASTM 兼容头部后 `odid_message_process_pack()` |
| GB 46750-2025 | `FA:0B:BC` / **`0xF0`** | `0xFF` + 版本号 + 数据长度 + 数据标识(3B) + 数据内容(变长)，**无 Message Counter** | 自定义解析 21 个数据内容项 |

> Wi-Fi Beacon 统一使用 OUI `FA:0B:BC`，但 **Vendor Type 两套标准不同**：
> ASTM / GB 42590 为 `0x0D`，**GB 46750 为 `0xF0`**。新国标若误用 `0x0D`，
> 专业检测设备会按 ASTM 报文解析后丢弃，表现为"完全收不到信号"（v1.1.1 修正项）。
> GB 46750 以 `data[1]==0xFF` 作为数据类型标识，数据标识固定 3 字节，
> 每字节 **bit0 为扩展位**（置 1 表示后面还有标识字节，故实际为 `0xDF 0xE5 0xFE` 链式指示），
> 支持全部 21 个数据内容项（001-021）。
> ASTM 与 GB 42590 在 Wire 格式上均以 `0xF1` 开头（ASTM 为位域编码
> `MessageType=0xF` + `ProtoVersion=1`，国标为独立 magic byte），
> 但语义上需要区分协议类型。

### 5.8 GHz 说明（仅 ESP32-C5）

GB 42590-2023 附录 A.1.3.1.3 允许 **5725–5829 MHz**，对应 Wi-Fi 信道 149–165。固件把 5.8G
段锁在 149–165（默认 149），启用 20 MHz 带宽，并在初始化时显式设置国家码 `CN`
（ESP-IDF 默认 `world-safe` 会裁掉绝大部分 5G 信道）。

| 标准 | 5.8G 功率要求 | C5 情况 |
|---|---|---|
| GB 42590-2023 | EIRP ≥ 11 dBm | ✅ 余量充足（C5 5G 最大输出 19 dBm，加天线增益后 EIRP 约 21 dBm） |
| GB 46750-2025 6.1.4 | 轻小型以上：峰值 ≥ 18 dBm、平均 ≥ 20 dBm | ⚠️ 卡在临界：19 dBm 是芯片峰值，802.11 的平均功率与区域功率配置、天线增益强相关，是否达标须实测；要求严格时加外置 PA |

> 注意：2.4G 与 5.8G 共用同一射频链路，不能同时工作；切换频段会重建热点，手机需重新连接。
> C5 同时只提供一条射频，5.8G 频段下 AP 与混杂监听仍然可以并存（已实测信道 149 收发正常）。

## 版本历史

### v1.2.0 — 5.8 GHz 双频段 / 单射频收发互斥

**5.8 GHz 支持（仅 ESP32-C5，编译期裁剪）**

- 新增 `CRID_HAS_5G_BAND`（由 `CONFIG_IDF_TARGET_ESP32C5` 推导）：C5 才编入
  `esp_wifi_set_band_mode()`、国家码与 5G 信道映射，**S3 固件里这部分代码整体被预处理裁掉**
  （实测 S3 的 elf 里 `set_band_mode` 符号出现 0 次、C5 出现 2 次），既不会链接到不存在的 API，
  页面也按 `has5g=false` 隐藏 5.8G 选项
- 频段可网页切换：2.4G 走 `channel`（1–13），5.8G 锁 **149–165**（国标 5725–5829 MHz，默认 149）
- 初始化顺序修正：C5 复位后默认 `5G_only`，必须先 `esp_wifi_start()` → 切 band → 再写目标信道，
  否则驱动直接拒绝 `Channel number 6 (2G) is not supported in current band mode`
- 显式设置国家码 `CN` 与 `WIFI_BW20`，满足国标对 5.8G 频段与带宽的要求
- 接收端换信道 / 换频段前先停掉混杂监听，避免 `set_channel` 与运行中的监听冲突

**单射频收发互斥（页面改为拨动开关）**

- 一条射频同一时刻只能发或只能收，原来的「发射 + 接收」双开实为分时轮转：Beacon 抢占空口的
  那几毫秒里到达的信号全部丢失，两边互相抢时间、掉帧 → 页面改为
  **「接收 · 监听周边 / 发射 · RID 广播」二选一拨动开关**，永远只有一边点亮
- 后端兜底 `crid_config_radio_mutex()`：旧配置双开时保留发射清掉接收，双关时给活跃频段补一个接收
- 两频段开关各自记忆（`tx_2g/rx_2g/tx_5g/rx_5g`），切换频段立即沿用该频段上次的选择
- 热点始终常开，与角色开关无关，随时可连进来改参数

**页面**

- 长说明全部改为原生 `details/summary` 折叠条（零 JS，右侧 ▾ 随展开旋转），默认收起，点开才显示
- 保存顺序改为「**先回包 → 300 ms → 重建 AP**」，修掉改 SSID / 信道 / 广播间隔时浏览器报
  `TypeError: Failed to fetch`（浏览器本身是该 AP 的 STA，重建 AP 会把它踢下线）

**配置存储**

- NVS 升级到 **v3**（`crid_persist_v3_t`），v2 配置自动迁移，新增 `band / channel_5g /
  tx_2g / rx_2g / tx_5g / rx_5g` 字段；出厂默认 = 2.4 GHz 发射

**工具与构建**

- `build.ps1`：`[CmdletBinding(PositionalBinding=$false)]` 修掉 `-p COMx` 与 `set-target` 抢
  位置 0 的问题；透出 `idf.py` 退出码（此前烧录失败仍返回 0）
- `extract_bin_page.py` 支持传入 bin 路径，`build_c5/` 等独立构建目录也能核对内嵌页面
- `jsdom_test.js` 增加角色拨动、5.8G 信道锁定、双关拦截三组断言

### v1.1.1 — 国标报文编码修正 / 接收数据完整化

**GB 46750-2025（新国标）报文修正** —— 此前专业检测设备完全收不到：

- **Vendor IE 类型 `0x0D` → `0xF0`**：原值会被检测设备当作 ASTM 报文解析后丢弃
- **去除 Message Counter**，报文改为 `0xFF` + 版本号 + 数据长度 + 3B 数据标识位图 + 内容
- 数据标识字节 **bit0 改为扩展位**（报文中为 `0xDF 0xE5 0xFE`，链式指示后续标识字节）
- 006 / 008 位置改为 **经度在前、纬度在后**（int32 ×1e-7，未知写 `0xFFFFFFFF`）
- 补入 **011 相对高度**（`raw/2 − 9000`）与 **012 垂直速度**（1 字节，bit7 为符号位）
- **020 时间戳**改为浏览器授时（`POST /api/time`，页面加载时与每 10 分钟同步），未授时写 0

**GB 42590-2023（旧国标）编码修正** —— 依据附录 A 逐字段核对：

- **垂直速度**：`v*2+63` → 8 位有符号 `v/0.5`（原实现解出来固定偏 +31.5 m/s）
- **地速高段**：`255+(v−63.75)/0.75` → `(v−63.75)/0.75`（uint8 溢出会变成标准"无效速度"）
- **运行区域半径**：`0x64` → `10`（1 LSB = 10 m，原值实际表示 1000 m）
- **航迹角无效值**：`255` → E/W=1 且编码 181（即 361°）
- **Beacon 间隔单位**：毫秒 → **TU（1.024 ms）** 换算，避免实际广播频率跌破 1 Hz

**接收端**：

- GB 46750 补齐全部 **21 个数据内容项**（相对高度、气压强高、水平/垂直/速度精度、时间戳、操作员高度等）
- 卡片新增「详情」折叠区，支持 **一键展开 / 折叠全部**，信号多时列表不再冗长

**修复**：

- HTTP 路由表已有 9 条而 `max_uri_handlers` 写死为 8，导致末尾 `/favicon.ico` **静默注册失败**（日志仅一行 warning，之后新增接口都会失效）→ 改为按路由表长度自动取值并记录注册失败
- 构建脚本 `-p COMx` 参数此前被 PowerShell 丢弃（两块板同时插着时会挑错芯片、误报 `This chip is ESP32-C5, not ESP32-S3`）→ 显式声明并转发；失败时返回非 0 退出码
- 串口日志 `Compile time` 长期显示旧值（`__DATE__/__TIME__` 变化 ninja 感知不到 + ccache `time_macros`）→ 构建前强制重编 app 描述单元

**新增工具**：

- `tools/check_gb46750.py`：按官方 libopendroneidcn 规则校验实机报文字节
- `tools/check_gb42590.py`：旧国标逐字段校验 + 全量程扫描（地速 / 垂直速度）
- `tools/cap_boot.py`：复位设备并抓取串口启动日志（原始字节直写，避免中文乱码）

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
