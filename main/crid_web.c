/**
 * @file crid_web.c
 * @brief 内置配置网页服务器：手机连接热点后访问 http://192.168.4.1
 *        可在线修改 UAS ID / 坐标 / 高度 / 速度 / 巡游参数 / 热点参数
 */

#include "crid_web.h"
#include "crid_wifi.h"
#include "crid_rx.h"
#include "crid_gb46750.h"
#include "page_gz.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CN_C-RID_WEB";


static cn_crid_config_t *s_config = NULL;

// 由 crid-sim.c 提供：模式/标准切换后的运行时应用
extern void crid_app_apply_mode(void);

// ================================================================
// 内置网页（全部使用单引号，避免 C 字符串转义）
// 地图双模式：手机有蜂窝数据时加载 Leaflet+OSM 在线地图选点；
// CDN 不可达时自动降级为内置离线网格拾取器（以当前基准点为中心）。
// ================================================================
static const char PAGE_HTML[] =
"<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32-RID 接收/发射</title>"
"<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>"
"<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js' onerror='window._mapfail=1'></script>"
"<style>"
"*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
"body{font-family:-apple-system,'Segoe UI',Roboto,'PingFang SC','Microsoft YaHei',sans-serif;"
"margin:0;background:#eef1f5;color:#1f2937;line-height:1.5;overflow-x:hidden}"
".hdr{background:linear-gradient(135deg,#0b3d2e,#0f8a4d);color:#fff;padding:16px}"
".hdr-in{max-width:1080px;margin:0 auto}"
".hdr h2{margin:0;font-size:20px;display:flex;align-items:center;gap:8px}"
".dot{width:10px;height:10px;border-radius:50%;background:#4ade80;display:inline-block;"
"box-shadow:0 0 6px #4ade80;flex:none}"
".hdr p{margin:4px 0 0;font-size:12px;opacity:.8}"
".wrap{max-width:1080px;margin:0 auto;padding:14px}"
".grid{display:grid;grid-template-columns:1fr;gap:14px}"
// 网格子项默认 min-width:auto，内部一旦有不可断行的长字符串（很长的机型名、
// 序列号），整列会被撑得比屏幕还宽，页面横向溢出，底部按钮就跑到屏幕外。
// 统一给 0 让子项可以收缩，长文本交给 overflow-wrap 断行。
".grid>*{min-width:0}"
".col{display:flex;flex-direction:column;gap:14px;min-width:0}"
// 列尾卡片拉伸补齐，两列强制等高
".col>.card:last-child{flex:1 1 auto}"
".card{background:#fff;border-radius:12px;padding:16px;box-shadow:0 1px 4px rgba(16,24,40,.08)}"
".card h3{margin:0 0 4px;font-size:15px;color:#0b3d2e;display:flex;align-items:center;gap:7px}"
".card h3::before{content:'';width:4px;height:14px;border-radius:2px;background:#0f8a4d;flex:none}"
"label{display:block;font-size:12px;color:#6b7280;margin-top:10px;font-weight:600}"
"input,select{width:100%;padding:9px 10px;margin-top:4px;border:1px solid #d1d5db;"
"border-radius:8px;font-size:15px;background:#f9fafb;color:#1f2937;transition:.15s}"
"input:focus,select:focus{outline:none;border-color:#0f8a4d;background:#fff;"
"box-shadow:0 0 0 3px rgba(15,138,77,.15)}"
".g{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}"
".note{font-size:12px;color:#9ca3af;margin:8px 0 0}"
// 说明信息默认折叠：details/summary 原生控件，零 JS，点开才展开完整文字
"details.tip{margin:10px 0 0;background:#f7f9fa;border:1px solid #e6eaee;border-radius:8px}"
"details.tip>summary{list-style:none;cursor:pointer;padding:7px 10px;display:flex;"
"align-items:center;gap:6px;font-size:12px;font-weight:600;color:#0f8a4d}"
"details.tip>summary::-webkit-details-marker{display:none}"
"details.tip>summary::after{content:'\\25BE';margin-left:auto;font-size:10px;"
"color:#9ca3af;transition:.15s}"
"details.tip[open]>summary::after{transform:rotate(180deg)}"
"details.tip>div{padding:0 10px 9px;font-size:12px;color:#6b7280;line-height:1.7}"
"details.tip>div b{color:#0b3d2e}"
"#map{height:320px;border-radius:10px;border:1px solid #e5e7eb;z-index:0}"
"#grid{width:100%;border-radius:10px;border:1px solid #e5e7eb;touch-action:manipulation}"
".seg{display:flex;gap:8px;margin:8px 0 10px}"
".seg label{margin:0;flex:1}"
".seg input{display:none}"
".seg span{display:flex;align-items:center;justify-content:center;text-align:center;"
"min-height:37px;padding:6px 8px;border-radius:8px;line-height:1.25;"
"border:1px solid #d1d5db;font-size:14px;font-weight:600;color:#374151;"
"background:#f9fafb;cursor:pointer;transition:.15s}"
".seg input:checked+span{background:#0f8a4d;border-color:#0f8a4d;color:#fff}"
".lbl{display:block;font-size:12px;color:#6b7280;margin:0 0 4px}"
".ckl{display:flex;align-items:center;gap:8px;cursor:pointer;margin:0;padding:8px 10px;"
"background:#f7f8fa;border:1px solid #e3e6ea;border-radius:8px}"
".ckl input{display:none}"
".ckl i{width:36px;height:20px;background:#cfd6df;border-radius:10px;position:relative;"
"transition:.15s;flex:none}"
".ckl i:after{content:'';position:absolute;left:2px;top:2px;width:16px;height:16px;"
"background:#fff;border-radius:50%;transition:.15s}"
".ckl input:checked+i{background:#0f8a4d}"
".ckl input:checked+i:after{left:18px}"
".ckl b{font-weight:600;font-size:12px;color:#6b7280}"
".ckl input:checked~b{color:#0f8a4d}"
".ckl.off{opacity:.4;pointer-events:none}"
".mpp{margin-top:10px}"
".dd{position:relative}"
".dd-btn{display:flex;justify-content:space-between;align-items:center;width:100%;"
"padding:9px 12px;margin-top:4px;border:1px solid #d1d5db;border-radius:8px;"
"background:#f9fafb;font-size:15px;color:#374151;cursor:pointer;transition:.15s;text-align:left}"
".dd-btn:hover{border-color:#0f8a4d}"
".dd.open .dd-btn{border-color:#0f8a4d;background:#fff;box-shadow:0 0 0 3px rgba(15,138,77,.15)}"
".dd-txt.ph{color:#9ca3af}"
".dd-car{color:#6b7280;font-size:12px;transition:.2s}"
".dd.open .dd-car{transform:rotate(180deg)}"
".dd-panel{display:none;position:absolute;z-index:60;left:0;right:0;top:calc(100% + 4px);"
"background:#fff;border:1px solid #e5e7eb;border-radius:10px;"
"box-shadow:0 8px 24px rgba(16,24,40,.16);overflow:hidden}"
".dd.open .dd-panel{display:block}"
".dd-list{max-height:240px;overflow-y:auto;padding:4px;-webkit-overflow-scrolling:touch}"
".dd-group{font-size:11px;font-weight:700;color:#9ca3af;padding:8px 10px 2px}"
".dd-item{padding:9px 10px;border-radius:7px;font-size:14px;color:#1f2937;cursor:pointer}"
".dd-item:hover{background:#ecfdf5;color:#0b3d2e}"
"#liveinfo{font-size:12px;color:#1565c0;margin-top:8px;min-height:14px;"
"font-family:monospace;background:#eff6ff;border-radius:6px;padding:5px 8px}"
"#hint{font-size:12px;color:#9ca3af;margin-top:8px}"
// ---- 接收监测 ----
".rxbar{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 4px}"
".chip{flex:1 1 76px;min-width:76px;background:#f6f8fa;border:1px solid #e5e7eb;border-radius:10px;"
"padding:8px 6px;text-align:center}"
".chip b{display:block;font-size:18px;color:#0b3d2e;line-height:1.25;font-variant-numeric:tabular-nums}"
".chip span{display:block;font-size:11px;color:#8b95a1;margin-top:2px}"
".chip.on b{color:#0f8a4d}"
".chip.warn b{color:#c0392b}"
".rxbanner{display:flex;align-items:center;gap:8px;font-size:12px;color:#6b7280;margin:12px 0 8px}"
".rxbanner .dot{width:8px;height:8px}"
".rxbanner .dot.live{animation:pulse 1.2s infinite}"
"@keyframes pulse{0%,100%{opacity:1}50%{opacity:.25}}"
".rxlist{display:flex;flex-direction:column;gap:9px;min-width:0}"
".rxsec{font-size:12px;font-weight:700;color:#0f8a4d;margin:2px 0 -4px;letter-spacing:.3px}"
".rxsec.dim{color:#9ca3af}"
".dcard{border:1px solid #e5e7eb;border-radius:12px;overflow:hidden;background:#fff;"
"min-width:0;box-shadow:0 1px 3px rgba(16,24,40,.06)}"
".dcard.hot{border-color:#f0b6ae;box-shadow:0 1px 6px rgba(192,57,43,.12)}"
// 离线目标卡片：整体灰度弱化，与在线目标一眼区分
".dcard.off{filter:grayscale(1);opacity:.62}"
// 卡片头：UAS ID + 协议/状态标签 + 信号柱，整行高度压到最小
".dh{display:flex;align-items:center;gap:7px;padding:6px 10px;background:#f6f8fa;"
"border-bottom:1px solid #e5e7eb}"
".dh .id{flex:1;min-width:0;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;"
"font-size:13px;font-weight:700;color:#111827;letter-spacing:.2px;overflow:hidden;"
"text-overflow:ellipsis;white-space:nowrap}"
".tag{flex:none;font-size:10px;font-weight:700;padding:2px 7px;border-radius:20px;line-height:1.6}"
".tag.g42590{background:#e0f2fe;color:#075985}"
".tag.g46750{background:#ede9fe;color:#5b21b6}"
".tag.astm{background:#f1f5f9;color:#475569}"
".tag.air{background:#dcfce7;color:#166534}"
".tag.pend{background:#fef3c7;color:#92400e}"
".sig{flex:none;display:flex;align-items:flex-end;gap:2px;height:14px}"
".sig i{width:3px;background:#d1d5db;border-radius:1px;display:block}"
".sig i.on{background:#0f8a4d}"
".sig i.on.w{background:#eab308}"
".sig i.on.weak{background:#c0392b}"
".db{padding:7px 10px 8px;min-width:0}"
// 机型标题行：左＝机型名，右＝「链路与统计」（MAC / RSSI / 帧数 / 最后收到）。
// 原来链路信息独占一整行格子，现在并进标题行，展开前高度省一行。
// 机型名来自对方广播、长度不可控：可收缩 + 任意断行，链路串超宽则换行仍右对齐。
".dname{display:flex;align-items:baseline;gap:8px;flex-wrap:wrap;"
"font-size:13px;color:#0b3d2e;font-weight:600;margin-bottom:1px}"
".dname .nm{min-width:0;overflow-wrap:anywhere;word-break:break-word}"
".dname .lnk{margin-left:auto;max-width:100%;overflow:hidden;text-overflow:ellipsis;"
"white-space:nowrap;font-size:10px;font-weight:500;color:#8b95a1;letter-spacing:-.1px;"
"font-family:ui-monospace,Menlo,Consolas,monospace}"
".kv{display:grid;grid-template-columns:repeat(3,1fr);gap:1px;margin-top:7px;"
"background:#e5e7eb;border:1px solid #e5e7eb;border-radius:9px;overflow:hidden}"
".kv div{background:#fff;padding:5px 7px;min-width:0}"
".kv b{display:block;font-size:13px;color:#111827;font-weight:700;"
"font-variant-numeric:tabular-nums;line-height:1.3}"
".kv b small{font-size:9px;font-weight:600;color:#9ca3af;margin-left:2px}"
".kv span{display:block;font-size:9px;color:#8b95a1}"
".kv .hl b{color:#0a7d43}"
// 副信息行：飞手位置与实名登记号并列一行（飞手坐标占主列，实名登记号按内容宽度收窄）
".kv2{display:grid;grid-template-columns:1fr auto;gap:1px;margin-top:6px;"
"background:#e5e7eb;border:1px solid #e5e7eb;border-radius:9px;overflow:hidden}"
".kv2 div{background:#f9fafb;padding:4px 7px;min-width:0}"
".kv2 div.wide{grid-column:1/-1}"
".kv2 b{display:block;font-size:11px;color:#1f2937;font-weight:700;"
"font-variant-numeric:tabular-nums;line-height:1.35;overflow-wrap:anywhere}"
".kv2 span{display:block;font-size:9px;color:#8b95a1}"
// ---- 导航按钮：跳转手机地图 App（高德/百度），点卡片自身仍做地图聚焦 ----
".navrow{display:flex;gap:8px;margin-top:7px;flex-wrap:wrap}"
".navbtn{flex:1;min-width:118px;padding:7px 10px;font-size:12px;font-weight:700;"
"font-family:inherit;color:#0b3d2e;background:#eefaf3;border:1px solid #bbe6cf;"
"border-radius:9px;cursor:pointer;line-height:1.3}"
".navbtn:active{transform:scale(.98);background:#dff5e9}"
".navbtn[data-nav=op]{color:#7c2d12;background:#fff5ec;border-color:#f5cfae}"
".navbtn[data-nav=op]:active{background:#ffe9d5}"
// 导航地图选择层：点「导航到…」按钮时才弹出，不占页面空间
".navmask{position:fixed;top:0;left:0;right:0;bottom:0;z-index:9999;display:none;"
"background:rgba(17,24,39,.45)}"
".navmask.on{display:flex;align-items:flex-end;justify-content:center}"
".navsheet{width:100%;max-width:460px;background:#fff;border-radius:14px 14px 0 0;"
"padding:14px 14px 16px;box-shadow:0 -6px 24px rgba(0,0,0,.18)}"
".navsheet h4{margin:0;font-size:15px;color:#111827}"
".navsheet .sub{margin:3px 0 12px;font-size:11px;color:#8b95a1;"
"font-family:ui-monospace,Menlo,Consolas,monospace;word-break:break-all}"
".navopt{display:block;width:100%;text-align:left;margin-bottom:8px;padding:11px 12px;"
"font-family:inherit;background:#f9fafb;border:1px solid #e5e7eb;border-radius:10px;"
"cursor:pointer}"
".navopt:active{transform:scale(.99);background:#eefaf3;border-color:#bbe6cf}"
".navopt b{display:block;font-size:14px;color:#0b3d2e}"
".navopt span{display:block;font-size:11px;color:#8b95a1;margin-top:2px}"
".navcancel{width:100%;padding:11px;font-family:inherit;font-size:13px;font-weight:600;"
"color:#6b7280;background:#fff;border:1px solid #e5e7eb;border-radius:10px;cursor:pointer}"
// ---- 报文详情折叠区与一键展开/折叠按钮 ----
".fbtn{flex:none;background:#fff;border:1px solid #d1d5db;border-radius:8px;"
"padding:6px 10px;font-size:12px;color:#0b3d2e;font-weight:600;cursor:pointer}"
".fbtn:active{transform:scale(.97)}"
".dtl{margin-top:9px;border-top:1px dashed #e5e7eb}"
".dtl-h{display:flex;align-items:center;gap:5px;padding:5px 2px 0;"
"font-size:11px;font-weight:700;color:#0f8a4d;cursor:pointer;user-select:none}"
".dtl-h i{font-style:normal;font-size:10px;color:#9ca3af;transition:.15s}"
".dcard.open .dtl-h i{transform:rotate(90deg)}"
".dtl-b{display:none;padding:6px 2px 2px}"
".dcard.open .dtl-b{display:block}"
".dtab{width:100%;border-collapse:collapse;font-size:11px}"
".dtab td{padding:4px 6px;border-bottom:1px dashed #f0f1f3;vertical-align:top}"
".dtab td:first-child{color:#8b95a1;white-space:nowrap;width:38%}"
".dtab td:last-child{color:#1f2937;font-family:ui-monospace,Menlo,Consolas,monospace;"
"word-break:break-all}"
".rxb0{text-align:center;padding:22px 10px;color:#9ca3af;font-size:13px;"
"background:#fafbfc;border:1px dashed #d1d5db;border-radius:12px}"
".rxb0 b{display:block;font-size:26px;margin-bottom:4px;opacity:.5}"
"@media(min-width:560px){.kv{grid-template-columns:repeat(6,1fr)}"
".kv .wide{grid-column:span 6}}"
// ---- 接收地图 ----
// z-index:0 让 .mapbox 自成层叠上下文：Leaflet 内部图层的 z-index 高达 200-800，
// 若不隔离就会盖住页面上的 fixed 弹层（导航选择层曾因此被地图压住）
".mapbox{position:relative;z-index:0;height:340px;border:1px solid #e5e7eb;border-radius:12px;"
"overflow:hidden;margin:10px 0 4px;background:#e8edf2}"
"#rxmap{position:absolute;inset:0}"
// 接收端离线网格：与发射端同样的 canvas 网格底图，热点无互联网时仍可看相对位置
"#rxgrid{display:none;width:100%;border-radius:10px;background:#fafafa}"
".rxoff{margin-top:8px;font-size:11px;color:#6b7280;line-height:1.6}"
".mapleg{position:absolute;left:8px;bottom:8px;z-index:500;background:rgba(255,255,255,.92);"
"border:1px solid #e5e7eb;border-radius:8px;padding:5px 8px;font-size:11px;color:#4b5563;"
"display:flex;gap:10px;align-items:center}"
".mapleg i{width:9px;height:9px;border-radius:50%;display:inline-block;margin-right:4px;vertical-align:-1px}"
".mapbtn{position:absolute;right:8px;top:8px;z-index:500;border:1px solid #e5e7eb;"
"background:rgba(255,255,255,.95);border-radius:8px;padding:5px 10px;font-size:12px;"
"color:#0b3d2e;font-weight:600;cursor:pointer}"
".mapbtn:active{background:#f3f4f6}"
// 底部动作栏：等分网格（不再用 flex:1 + 34% 混排）。
// 混排时两按钮宽度之和含 gap 后容易超出容器，窄屏上会顶出右边界。
".actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;min-width:0}"
".actions>button{width:100%;min-width:0}"
"button{padding:13px 8px;border:0;border-radius:10px;cursor:pointer;transition:.15s;"
"background:linear-gradient(135deg,#0f8a4d,#0a7d43);color:#fff;font-size:16px;font-weight:600;"
"font-family:inherit}"
"button:hover{filter:brightness(1.08)}"
"button:active{transform:scale(.98)}"
"button.danger{background:#fff;color:#c0392b;border:1px solid #f3b6b0}"
"button.danger:hover{background:#fdf1f0;filter:none}"
"#msg{margin-top:10px;font-size:14px;min-height:18px;font-weight:600}"
".foot{font-size:12px;color:#9ca3af;margin:10px 2px}"
"@media(min-width:860px){"
".grid{grid-template-columns:1fr 1fr}"
".span2{grid-column:1/-1}"
"#map{height:460px}"
"}"
"</style></head><body>"
"<div class='hdr'><div class='hdr-in'>"
"<h2><span class='dot'></span>ESP32-RID 接收/发射</h2>"
"<p>开源项目根据 GB 42590-2023 / GB 46750-2025</p>"
"</div></div>"
"<div class='wrap'><div class='grid'>"
"<div class='card span2'><h3>运行模式</h3>"
"<span class='lbl'>工作角色（拨动选择，单射频二选一）</span>"
"<div class='seg'>"
"<label><input type='radio' name='role' value='rx'><span>接收<br>监听周边</span></label>"
"<label><input type='radio' name='role' value='tx'><span>发射<br>RID 广播</span></label>"
"</div>"
"<span class='lbl'>Wi-Fi 频段（单射频，二选一）</span>"
"<div class='seg'>"
"<label><input type='radio' name='band' value='0' checked><span>2.4 GHz</span></label>"
"<label id='b5gl'><input type='radio' name='band' value='1'><span>5.8 GHz</span></label>"
"</div>"
"<details class='tip'><summary>说明：为什么射频信号只能二选一</summary><div>"
"本机只有<b>一条射频链路</b>，同一时刻只能发或只能收。发射时射频忙于发出 Beacon（携带 RID 报文），"
"这段时间到达的信号收不到；接收时射频一直在监听，就没有空口资源稳定广播。"
"所以「又发又收」在单射频硬件上只能是分时轮转，会互相抢时间、掉帧，不如明确二选一。"
"<br>热点本身<b>始终开启</b>，与射频信号怎么用无关，手机随时可以连进来改参数。"
"<br>2.4G 与 5.8G 也共用同一射频，只能启用一个频段；切换频段会重建热点，手机需重新连接。"
"</div></details>"
"<div id='stdbox'>"
"<label>广播标准</label>"
"<div class='seg'>"
"<label><input type='radio' name='std' value='0'><span>GB42590<br>旧国标</span></label>"
"<label><input type='radio' name='std' value='1'><span>GB46750<br>新国标</span></label>"
"</div>"
"<details class='tip'><summary>说明：两个国标的区别</summary><div>"
"<b>旧国标</b> = GB 42590-2023（OpenDroneID 打包报文，兼容绝大多数现有接收端）；"
"<b>新国标</b> = GB 46750-2025（位图数据项报文，2026-05-01 实施，含实名登记标志）。"
"切换在「保存并应用」后生效，下方身份信息只显示当前标准实际广播的字段。"
"</div></details>"
"</div>"
"</div>"
// 两列自平衡布局：左列身份信息，右列飞行参数+飞手位置+巡游参数，
// 消除两列视图下飞行参数卡片下方的大块空白
"<div class='col txonly'>"
"<div class='card txonly'><h3>身份信息</h3>"
"<label>无人机唯一 ID (UAS ID)</label><input id='uas_id' maxlength='24'>"
"<label>机型名称 (Self-ID)</label><input id='drone_name' maxlength='20'>"
"<p class='note' style='margin-top:2px'>仅旧国标广播（新国标报文无此数据项），两个标准下均可填写记录。</p>"
"<div class='oldstd'><label>机型代码 (UA Type)</label>"
"<div class='dd' id='dd2'>"
"<div class='dd-btn' id='dd2btn'><span class='dd-txt ph' id='dd2txt'>-- 选择机型代码 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='dd2list'></div></div></div>"
"<input type='hidden' id='ua_type'></div>"
"<label>大疆机型预设 (伪装)</label>"
"<div class='dd' id='dd'>"
"<div class='dd-btn' id='ddbtn'><span class='dd-txt ph' id='ddtxt'>-- 选择型号自动填充 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='ddlist'></div></div></div>"
"<details class='tip'><summary>说明：预设如何生成序列号</summary><div>"
"预设按大疆 RID 序列号前 8 位（1581F + 型号码）生成 20 位序列号，并填入机型名称与机型代码；"
"机型代码为 RID 标准 16 类航空器类别码。序列号可手动微调，其他品牌用「其他厂商」分组。"
"两个标准均广播该序列号。</div></details>"
"<div class='newstd'><div class='g'>"
"<div><label>实名登记号后8位</label><input id='realname_id' maxlength='8'>"
"<p class='note' style='margin-top:2px'>仅新国标广播，空 = 不填。</p></div>"
"<div><label>新国标分类</label>"
"<div class='dd' id='dd4'>"
"<div class='dd-btn' id='dd4btn'><span class='dd-txt ph' id='dd4txt'>-- 选择分类 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='dd4list'></div></div></div>"
"<input type='hidden' id='ua_category_gb'></div>"
"</div></div>"
"<div class='oldstd'><label>操作员 ID</label><input id='operator_id' maxlength='20'></div>"
"<label>热点名称 (SSID)</label><input id='ssid' maxlength='31'>"
"<div class='g'>"
"<div><label id='chlbl'>Wi-Fi 信道 (1-13)</label><input id='channel' type='number' min='1' max='13'></div>"
"<div><label>广播间隔 (ms)</label><input id='beacon_interval_ms' type='number' min='200' max='60000'></div>"
"</div></div>"
"</div>"
"<div class='col txonly'>"
"<div class='card txonly'><h3>飞行参数</h3><div class='g'>"
"<div><label>基准纬度</label><input id='base_latitude' type='number' step='0.0000001'></div>"
"<div><label>基准经度</label><input id='base_longitude' type='number' step='0.0000001'></div>"
"<div><label>海拔高度 m (MSL)</label><input id='base_altitude_msl' type='number'></div>"
"<div><label>相对高度 m (AGL)</label><input id='altitude_agl' type='number'></div>"
"<div><label>水平速度 m/s</label><input id='speed_horizontal' type='number' step='0.1'></div>"
"<div><label>运行状态</label>"
"<div class='dd' id='dd3'>"
"<div class='dd-btn' id='dd3btn'><span class='dd-txt ph' id='dd3txt'>-- 选择状态 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='dd3list'></div></div></div>"
"<input type='hidden' id='status'></div>"
"</div></div>"
"<div class='card txonly'><h3>飞手位置</h3><div class='g'>"
"<div><label>飞手纬度</label><input id='operator_latitude' type='number' step='0.0000001'></div>"
"<div><label>飞手经度</label><input id='operator_longitude' type='number' step='0.0000001'></div>"
"</div>"
"<label>飞手海拔高度 m</label><input id='operator_altitude' type='number'>"
"<p class='note'>修改飞手坐标后，无人机位置自动按默认相对偏移生成；单独修改无人机位置不影响飞手。</p></div>"
"<div class='card txonly'><h3>巡游参数</h3><div class='g'>"
"<div><label>巡游速度系数</label><input id='patrol_speed' type='number' step='0.01'></div>"
"<div><label>纬度半径 (度)</label><input id='patrol_radius_lat' type='number' step='0.00001'></div>"
"<div><label>经度半径 (度)</label><input id='patrol_radius_lon' type='number' step='0.00001'></div>"
"</div><p class='note' id='patrolCalc'></p></div>"
"</div>"
"<div class='card span2 txonly'><h3>地图选点</h3>"
"<div class='seg'>"
"<label><input type='radio' name='pt' value='op' checked><span>选点：飞手</span></label>"
"<label><input type='radio' name='pt' value='drone'><span>选点：无人机</span></label>"
"</div>"
"<div id='map'></div>"
"<div id='gridbox' style='display:none'>"
"<canvas id='grid' width='320' height='320'></canvas>"
"<div class='mpp'><label>比例尺 (米/像素)</label><select id='mpp'>"
"<option value='5'>5</option><option value='20' selected>20</option>"
"<option value='100'>100</option><option value='500'>500</option></select></div>"
"</div>"
"<div id='liveinfo'></div>"
"<details class='tip'><summary>说明：地图图例与离线网格</summary><div>"
"红点=飞手，绿点=无人机，蓝点=实时广播位置，蓝线=飞行轨迹（最近约 8 分钟），绿圈=巡游范围。"
"<br>地图以飞手位置为中心：选点对象为「飞手」时点击/拖动会同步按默认偏移生成无人机位置；"
"选「无人机」时只改无人机位置。在线地图需手机保持移动数据，无网络时自动切换离线网格。"
"</div></details>"
"</div>"
"<div class='card span2' id='rxbox' style='display:none'><h3>接收监测</h3>"
"<div class='rxbar'>"
"<div class='chip'><b id='s_online'>0</b><span>在线目标</span></div>"
"<div class='chip on'><b id='s_rid'>0</b><span>RID 帧</span></div>"
"<div class='chip'><b id='s_total'>0</b><span>总帧数</span></div>"
"<div class='chip'><b id='s_ok'>0</b><span>解析成功</span></div>"
"<div class='chip warn' id='s_ovfbox'><b id='s_ovf'>0</b><span>队列溢出</span></div>"
"<button class='fbtn' id='foldall' type='button'>全部展开 ▾</button>"
"</div>"
"<div class='rxbanner'><span class='dot live'></span>"
"<span id='rxstate'>正在监听…</span></div>"
// 接收端信号地图容器：默认隐藏，进入接收模式且 /rxmap.js 加载成功后才显示。
// 容器 id 与发射端地图（map）完全不同，两张地图各自独立实例、互不干扰。
"<div class='mapbox' id='rxmapbox' style='display:none'><div id='rxmap'></div>"
"<canvas id='rxgrid' width='320' height='320'></canvas>"
"<div class='mapleg'>"
"<span><i style='background:#5b21b6'></i>GB46750</span>"
"<span><i style='background:#0369a1'></i>GB42590</span>"
"<span><i style='background:#6b7280'></i>ASTM</span>"
"<span><i style='background:#e67e22;border-radius:3px;transform:rotate(45deg)'></i>飞手位置</span>"
"<span><i style='background:#1565c0;height:2px;border-radius:1px'></i>飞行轨迹</span>"
"</div>"
"<button class='mapbtn' id='rxfit' type='button'>全览</button>"
"</div>"
"<div class='rxoff' id='rxgridnote'></div>"
"<div class='rxlist' id='rxlist'></div>"
"<details class='tip'><summary>说明：接收列表与离线规则</summary><div>"
"监听信道跟随上方「Wi-Fi 信道」设置（保存后生效），只能收到与本机同信道的广播。"
"卡片按信号强度排序，<b>绿色标题</b>为机型/描述，标题行右侧为链路与统计"
"（MAC · 信号强度 · 累计帧数 · 最后收到），<b>格内小字</b>为字段名；"
"飞手位置与实名登记号同一行；点击卡片在地图上定位。"
"操作员 ID、机型分类、精度、时间戳等<b>全部字段</b>都收在「报文详情」里，点标题展开。"
"10 秒无信号标记为已离线（灰度显示，保留 30 分钟），列表 2 秒刷新。"
"<br>卡片内「导航到无人机 / 导航到飞手」按钮点击后弹出高德 / 百度二选一，"
"选定后<b>直接唤起已安装的地图 App</b>并进入驾车路线（高德 amapuri / 百度 bdapp 原生协议），"
"目的地取机型名与经纬度（WGS-84 原始值，由 App 自行转 GCJ-02）；"
"只有 App 未安装（1.5 秒内没跳走）才退回网页版。</div></details>"
"</div>"
"<div class='actions span2'>"
"<button onclick='save()'>保存并应用</button>"
"<button class='danger' onclick='resetCfg()'>恢复初始</button>"
"</div>"
"<div id='msg' class='span2'></div>"
"</div>"
// 地图选择弹层：点卡片里的「导航到无人机 / 飞手」才出现，选完立即关闭
"<div class='navmask' id='navpick'>"
"<div class='navsheet'>"
"<h4 id='navpick_t'>导航到目标</h4>"
"<p class='sub' id='navpick_p'></p>"
"<button class='navopt' type='button' data-prov='amap'><b>高德地图 App</b>"
"<span>直接打开高德并进入驾车路线；未安装则退回高德网页版</span></button>"
"<button class='navopt' type='button' data-prov='bd'><b>百度地图 App</b>"
"<span>直接打开百度地图并进入驾车路线；未安装则退回百度网页版</span></button>"
"<button class='navcancel' type='button' id='navpick_c'>取消</button>"
"</div></div>"
"<p class='foot'>手机连接本机热点后访问 http://192.168.4.1 打开此页。"
"修改 SSID / 信道 / 广播间隔保存后需重新连接热点。配置已持久化，断电不丢失。</p>"
"</div>"
"<script>"
// 地图变量前置声明：save()/drawCircle() 等函数引用这些变量，
// 若声明留在脚本后段，一旦前段任何顶层语句抛错，变量将停留在
// 暂时性死区（部分 WebView 会报 circle 未定义），提前声明杜绝此类问题
"let map=null,opMarker=null,dMarker=null,live=null,circle=null,ginit=false;"
"let trailPts=[],trailLine=null;"   // 飞行轨迹：poll 每 2 秒记录广播位置连成线
"const F=['uas_id','drone_name','operator_id','realname_id','ssid','channel','beacon_interval_ms','ua_type',"
"'base_latitude','base_longitude','base_altitude_msl','altitude_agl','speed_horizontal',"
"'operator_latitude','operator_longitude','operator_altitude',"
"'status','patrol_speed','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];"
"function msg(t,ok){const m=document.getElementById('msg');m.textContent=t;"
"m.style.color=ok?'#0a7d43':'#c0392b';}"
"function fld(k){return document.getElementById(k);}"
"function baseLL(){const la=parseFloat(fld('base_latitude').value)||0;"
"const lo=parseFloat(fld('base_longitude').value)||0;return [la,lo];}"
"function opLL(){const la=parseFloat(fld('operator_latitude').value)||0;"
"const lo=parseFloat(fld('operator_longitude').value)||0;return [la,lo];}"
// 无人机基准点相对飞手的默认偏移（度），取自 crid_config.c 出厂默认值之差
"const D_OFF=[0.00287,0.00026];"
"function pickLL(la,lo){fld('base_latitude').value=la.toFixed(7);"
"fld('base_longitude').value=lo.toFixed(7);}"
"function autoDrone(la,lo){pickLL(la+D_OFF[0],lo+D_OFF[1]);}"
"function ptarget(){const r=document.querySelector('input[name=pt]:checked');"
"return r?r.value:'op';}"
"function syncMarkers(){const d=baseLL(),o=opLL();"
"if(opMarker)opMarker.setLatLng(o);"
"if(dMarker)dMarker.setLatLng(d);"
"if(map&&opMarker)map.panTo(o);"
// 地图函数在部分手机浏览器恢复休眠标签页后可能丢失（V8 上下文部分丢弃），
// 调用点全部加 typeof 防护：地图失效只降级，绝不阻塞参数编辑
"if(typeof drawCircle==='function')drawCircle();"
"if(!map){gbase=o;gpick=null;if(typeof drawGrid==='function')drawGrid();}}"
"function applyPick(t,la,lo){"
"if(t==='op'){fld('operator_latitude').value=la.toFixed(7);"
"fld('operator_longitude').value=lo.toFixed(7);autoDrone(la,lo);}"
"else{pickLL(la,lo);}"
"syncMarkers();}"
// 手动编辑飞手坐标输入框时，实时自动生成无人机位置
"function bindAuto(id){fld(id).addEventListener('input',function(){"
"const la=parseFloat(fld('operator_latitude').value);"
"const lo=parseFloat(fld('operator_longitude').value);"
"if(!isNaN(la)&&!isNaN(lo)){autoDrone(la,lo);syncMarkers();}});}"
"bindAuto('operator_latitude');bindAuto('operator_longitude');"
// 巡游实况换算：半径(度) + 系数(弧度/10秒) -> 轨迹真实线速度，用于和「水平速度」对照
"function patrolCalc(){"
"const R=111320,la=parseFloat(fld('base_latitude').value)||0;"
"const rl=parseFloat(fld('patrol_radius_lat').value)||0;"
"const ro=parseFloat(fld('patrol_radius_lon').value)||0;"
"const om=(parseFloat(fld('patrol_speed').value)||0)/10;"
"const vy=rl*R*om,vx=ro*R*Math.cos(la*Math.PI/180)*om;"
"const v=Math.sqrt(vx*vx+vy*vy);"
"const rep=parseFloat(fld('speed_horizontal').value);"
"const el=fld('patrolCalc');if(!el)return;"
"el.textContent='轨迹真实线速度约 '+v.toFixed(2)+' m/s；报文上报的「水平速度」为 '"
"+(isNaN(rep)?'--':rep.toFixed(1))+' m/s（由飞行参数里的输入决定，巡游不会改写它）';}"
"['patrol_speed','patrol_radius_lat','patrol_radius_lon','base_latitude','speed_horizontal']"
".forEach(function(id){fld(id).addEventListener('input',patrolCalc);});"
// ---- 大疆机型预设：前 8 位序列号前缀 -> 型号（数据源自社区逆向汇总，1581F 为大疆统一前缀）----
"const DJI=["
"['消费级',[['1581F8LQ','Mavic 4 Pro'],['1581F67Q','Mavic 3 Pro'],['1581F5Y8','Mavic 3 Classic'],"
"['1581F45Q','Mavic 3'],['1581F45T','Mavic 3'],['1581F895','Air 3S'],['1581F6N8','Air 3'],"
"['1581F385','Air 2S'],['1581FANL','Mini 5 Pro'],['1581F9DE','Mini 5 Pro'],['1581F5QJ','Mini 4 Pro'],"
"['1581F6Z9','Mini 4 Pro'],['1581F8C8','Mini 4K'],['1581F4XF','Mini 3 Pro'],['1581F5YH','Mini 3'],"
"['1581F6CD','Mini 2 SE'],['1581FA8J','Avata 360'],['1581FBV5','Lito 1'],['1581FB34','Lito X1']]],"
"['穿越机',[['1581F6W8','Avata 2'],['1581F4CQ','Avata'],['1581F4QW','Avata'],['1581FA6Q','Neo 2'],"
"['1581F8A1','Neo'],['1581F87L','Neo'],['1581F3CQ','DJI FPV'],['1581F7V2','DJI Flip']]],"
"['行业级',[['1581F6H8','Matrice 350 RTK'],['1581F5BK','Matrice 30'],['1581F5BM','Matrice 30T'],"
"['1581F52Q','Mavic 3E/3T'],['1581F5FH','Mavic 3E/3T'],['1581F5FJ','Mavic 3M'],['1581F578','Inspire 3'],"
"['1581F4Z4','Inspire 3'],['1581F6GK','Matrice 300 Series'],['1581F6Q8','Matrice 3D'],"
"['1581F6QA','Matrice 3TD'],['1581F9HE','Matrice 4T/4E'],['1581F7K3','Matrice 4T/4E'],"
"['1581F8HH','Matrice 4D/TD'],['1581F8HG','Matrice 4D/TD'],['1581F8DB','Matrice 400'],"
"['1581F986','Mavic 4 Series'],['1581FAN4','FlyCart 100']]],"
"['其他厂商',[['1748FEV3','Autel EVO MAX']]]];"
"(function(){const dd=fld('dd'),bt=fld('ddbtn'),tx=fld('ddtxt'),ls=fld('ddlist');"
"function pick(nm){const ps=[];"
"DJI.forEach(function(g){g[1].forEach(function(m){if(m[1]===nm)ps.push(m[0]);});});"
"const p=ps[Math.floor(Math.random()*ps.length)];"
"const cs='ABCDEFGHJKLMNPQRSTUVWXYZ0123456789';let sf='';"
"for(let i=0;i<12;i++){sf+=cs[Math.floor(Math.random()*cs.length)];}"
"fld('uas_id').value=(p+sf).substring(0,20);"
"fld('drone_name').value=nm;"
"fld('ua_type').value='2';uatSync();"
"tx.textContent=nm;tx.classList.remove('ph');"
"msg('已按 '+nm+' 生成预设（前缀 '+p+'）：序列号/机型名称/机型代码已填充，可手动微调后保存',true);}"
"DJI.forEach(function(g){const hd=document.createElement('div');"
"hd.className='dd-group';hd.textContent=g[0];ls.appendChild(hd);"
"const seen={};g[1].forEach(function(m){if(seen[m[1]])return;seen[m[1]]=1;"
"const it=document.createElement('div');it.className='dd-item';it.textContent=m[1];"
"it.onclick=function(){pick(m[1]);dd.classList.remove('open');};"
"ls.appendChild(it);});});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
// ---- 运行状态下拉 ----
"const ST=[['0','未声明'],['1','地面'],['2','空中'],['3','紧急'],['4','识别故障']];"
"function statusSync(){const v=String(fld('status').value);let lb='';"
"ST.forEach(function(u){if(u[0]===v)lb=u[1];});"
"const t=fld('dd3txt');"
"if(lb){t.textContent=lb;t.classList.remove('ph');}"
"else{t.textContent='-- 选择状态 --';t.classList.add('ph');}}"
"(function(){const dd=fld('dd3'),bt=fld('dd3btn'),ls=fld('dd3list');"
"ST.forEach(function(u){const it=document.createElement('div');it.className='dd-item';"
"it.textContent=u[1];"
"it.onclick=function(){fld('status').value=u[0];statusSync();dd.classList.remove('open');};"
"ls.appendChild(it);});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
"const UAT=[['0','未声明 (None)'],['1','固定翼 (Aeroplane)'],['2','多旋翼/直升机 (Multirotor)'],"
"['3','旋翼机 (Gyroplane)'],['4','垂直起降固定翼 (VTOL)'],['5','扑翼机 (Ornithopter)'],"
"['6','滑翔机 (Glider)'],['7','风筝 (Kite)'],['8','自由气球 (Free Balloon)'],"
"['9','系留气球 (Captive Balloon)'],['10','飞艇 (Airship)'],['11','降落伞 (Free Fall/Parachute)'],"
"['12','火箭 (Rocket)'],['13','系留动力飞行器 (Tethered)'],['14','地面障碍物 (Ground Obstacle)'],"
"['15','其他 (Other)']];"
"function uatSync(){const v=String(fld('ua_type').value);let lb='';"
"UAT.forEach(function(u){if(u[0]===v)lb=u[1];});"
"const t=fld('dd2txt');"
"if(lb){t.textContent=v+' - '+lb;t.classList.remove('ph');}"
"else{t.textContent='-- 选择机型代码 --';t.classList.add('ph');}}"
"(function(){const dd=fld('dd2'),bt=fld('dd2btn'),ls=fld('dd2list');"
"UAT.forEach(function(u){const it=document.createElement('div');it.className='dd-item';"
"it.textContent=u[0]+' - '+u[1];"
"it.onclick=function(){fld('ua_type').value=u[0];uatSync();dd.classList.remove('open');};"
"ls.appendChild(it);});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
// ---- 新国标分类下拉（GB46750 数据项 004，0=微型 ~ 4=大型）----
"const GBC=[['0','微型'],['1','轻型'],['2','小型'],['3','中型'],['4','大型']];"
"function gbcSync(){const v=String(fld('ua_category_gb').value);let lb='';"
"GBC.forEach(function(u){if(u[0]===v)lb=u[1];});"
"const t=fld('dd4txt');"
"if(lb){t.textContent=v+' - '+lb;t.classList.remove('ph');}"
"else{t.textContent='-- 选择分类 --';t.classList.add('ph');}}"
"(function(){const dd=fld('dd4'),bt=fld('dd4btn'),ls=fld('dd4list');"
"GBC.forEach(function(u){const it=document.createElement('div');it.className='dd-item';"
"it.textContent=u[0]+' - '+u[1];"
"it.onclick=function(){fld('ua_category_gb').value=u[0];gbcSync();dd.classList.remove('open');};"
"ls.appendChild(it);});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
"async function load(){try{const r=await fetch('/api/config');const j=await r.json();"
"F.forEach(k=>{if(j[k]!==undefined&&j[k]!==null)fld(k).value=j[k];});"
"LASTCFG=j;"
"document.querySelectorAll('input[name=band]').forEach(function(r){r.checked=(+r.value===(+j.band||0));});"
"if(!j.has5g){const b5=document.getElementById('b5gl');if(b5)b5.style.display='none';}"
"setRole((+j.band===1)?!!j.tx_5g:!!j.tx_2g);"
"document.querySelectorAll('input[name=std]').forEach(function(r){r.checked=(+r.value===j.rid_standard);});"
"uatSync();statusSync();gbcSync();bandUI();modeUI();stdUI();"
"if(typeof patrolCalc==='function')patrolCalc();"
"if(roleTx()&&typeof initMap==='function')initMap();}"
"catch(e){msg('加载失败: '+e,false);}}"
"const NUM=['channel','beacon_interval_ms','ua_type','base_latitude','base_longitude',"
"'base_altitude_msl','operator_latitude','operator_longitude','operator_altitude',"
"'altitude_agl','speed_horizontal','status',"
"'patrol_speed','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];"
"async function save(){const d={};F.forEach(function(k){"
"const v=fld(k).value;"
"d[k]=NUM.indexOf(k)>=0?(v===''?null:parseFloat(v)):v;});"
"const _b=+getRadio('band'),_L=LASTCFG||{};"
"const _tx=roleTx()?1:0,_rx=_tx?0:1;"
"d.band=_b;d.rid_standard=+getRadio('std');"
"d.tx_2g=_b===0?_tx:(_L.tx_2g!==undefined?_L.tx_2g:1);"
"d.rx_2g=_b===0?_rx:(_L.rx_2g!==undefined?_L.rx_2g:0);"
"d.tx_5g=_b===1?_tx:(_L.tx_5g!==undefined?_L.tx_5g:1);"
"d.rx_5g=_b===1?_rx:(_L.rx_5g!==undefined?_L.rx_5g:0);"
"d.channel_5g=149;"
"if(_b===1)d.channel=(_L.channel||6);"
"try{const r=await fetch('/api/config',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});"
"const j=await r.json();"
"if(j.ok){msg('已保存并应用 ✓'+(j.restarted?' (SSID/信道/广播间隔变更会重建热点，连接可能短暂中断，请稍候或重新连接)':''),true);"
// 保存成功后的地图刷新只是附带 UI 更新：包裹 try + typeof 防护，
// 无论页面 JS 环境处于什么状态都绝不影响「已保存」的判定与提示
"try{if(map&&circle){map.removeLayer(circle);circle=null;}"
"if(typeof drawCircle==='function')drawCircle();}catch(me){}}"
"else{msg('保存失败: '+(j.err||''),false);}}"
"catch(e){msg(e&&e.name==='TypeError'?'热点参数已生效，连接被重置，请重新连接热点后刷新确认':'保存失败: '+e,false);}}"
"async function resetCfg(){if(!confirm('确认恢复全部初始设定？当前配置将被覆盖。'))return;"
"try{const r=await fetch('/api/reset',{method:'POST'});const j=await r.json();"
"if(j.ok){msg('已恢复初始设定 ✓'+(j.restarted?' (热点参数变化，请重新连接热点)':''),true);"
"await load();}else{msg('恢复失败',false);}}"
"catch(e){msg(e&&e.name==='TypeError'?'已恢复初始设定，连接被重置，请重新连接热点后刷新确认':'恢复失败: '+e,false);}}"
// ---- 模式切换 UI 与接收监测轮询 ----
"function getRadio(n){const r=document.querySelector('input[name='+n+']:checked');"
"return r?r.value:'0';}"
// ---- 按广播标准显隐身份字段（旧国标独有 / 新国标独有） ----
"function stdUI(){const s=getRadio('std');"
"const oldOn=(s!=='1'),newOn=(s!=='0');"
"document.querySelectorAll('.oldstd').forEach(function(e){e.style.display=oldOn?'block':'none';});"
"document.querySelectorAll('.newstd').forEach(function(e){e.style.display=newOn?'block':'none';});}"
"document.querySelectorAll('input[name=std]').forEach(function(r){"
"r.addEventListener('change',stdUI);});"
"let rxTimer=null;let LASTCFG={};"
// 工作角色：单射频同一时刻只能发或收，页面用二选一拨动开关承载
"function roleTx(){const r=document.querySelector('input[name=role]:checked');"
"return r?r.value==='tx':true;}"
"function setRole(tx){document.querySelectorAll('input[name=role]').forEach(function(r){"
"r.checked=(r.value===(tx?'tx':'rx'));});}"
"function bandUI(){const b=getRadio('band');const c=fld('channel');if(!c)return;"
"const lb=document.getElementById('chlbl');"
"if(b==='1'){c.value=149;c.disabled=true;"
"if(lb)lb.textContent='Wi-Fi 信道 (149-165)';}else{c.disabled=false;"
"if(lb)lb.textContent='Wi-Fi 信道 (1-13)';"
"if(+c.value>13)c.value=(LASTCFG&&LASTCFG.channel)||6;}}"
"function modeUI(){const tx=roleTx();const rx=!tx;"
"document.getElementById('stdbox').style.display=tx?'block':'none';"
// txonly 恢复显隐用空串（回退样式表 display）：.col 列容器依赖 flex 布局，
// 若写死 block 会覆盖 flex 导致两列布局塌掉
"document.querySelectorAll('.txonly').forEach(function(e){e.style.display=tx?'':'none';});"
"document.getElementById('rxbox').style.display=rx?'block':'none';"
"if(rx&&!rxTimer){pollRx();rxTimer=setInterval(pollRx,2000);}"
"if(!rx&&rxTimer){clearInterval(rxTimer);rxTimer=null;}"
// 接收端地图：进入接收模式时才按需加载独立脚本，且全程 try 保护，
// 地图出问题绝不会影响模式切换、参数配置与接收列表
"if(rx){rxMapLoad();try{if(window.RXMAP)RXMAP.resize();}catch(e){}}"
"if(tx&&!ginit&&typeof initMap==='function')initMap();}"
"document.querySelectorAll('input[name=band]').forEach(function(r){"
"r.addEventListener('change',bandUI);});"
"document.querySelectorAll('input[name=role]').forEach(function(r){"
"r.addEventListener('change',modeUI);});"
// ---- 接收端地图加载器：仅负责把 /rxmap.js 拉进来，逻辑全在独立脚本里 ----
"let _rxmAsked=false;"
"function rxMapFail(msg){try{"
"const b=q('rxmapbox');if(b)b.style.display='none';"
"let n=q('rxmapfail');"
"if(!n){n=document.createElement('div');n.className='note';n.id='rxmapfail';"
"const host=q('rxbox');if(host&&q('rxlist'))host.insertBefore(n,q('rxlist'));}"
"if(n)n.textContent=msg;}catch(e){}}"
"function rxMapLoad(){"
"if(_rxmAsked||window.RXMAP)return;_rxmAsked=true;"
"const s=document.createElement('script');s.src='/rxmap.js?v=1';"
"s.onload=function(){try{if(window.RXMAP)RXMAP.boot();}"
"catch(e){rxMapFail('地图初始化失败，已降级为纯列表模式');}};"
"s.onerror=function(){_rxmAsked=false;"
"rxMapFail('地图脚本未加载（需联网），已降级为纯列表模式');};"
"document.head.appendChild(s);}"
// ---- 接收监测：卡片式渲染（风格与发射端卡片统一） ----
// ---- 接收监测：卡片式渲染（风格与发射端卡片统一） ----
"const RXST=['未报告','地面','空中','紧急','识别失效','识别失效'];"
"const UATN=['未声明','固定翼','多旋翼','旋翼机','垂直起降固定翼','扑翼机','滑翔机','风筝',"
"'自由气球','系留气球','飞艇','降落伞','火箭','系留动力飞行器','地面障碍物','其他'];"
"const GBCN=['微型','轻型','小型','中型','大型'];"
"function q(id){return document.getElementById(id);}"
"function esc(s){return String(s===undefined||s===null?'':s)"
".replace(/[&<>\"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c];});}"
"function f1(v){return (v===null||v===undefined||isNaN(v))?'--':Number(v).toFixed(1);}"
"function f6(v){return (v===null||v===undefined||isNaN(v))?'--':Number(v).toFixed(6);}"
"function ageTxt(ms){const s=Math.floor(ms/1000);"
"return s<60?(s+' 秒前'):(s<3600?(Math.floor(s/60)+' 分前'):(Math.floor(s/3600)+' 时前'));}"
"function rxProto(p){"
"if(p===4)return {c:'g46750',t:'GB46750',col:'#5b21b6'};"
"if(p===3)return {c:'g42590',t:'GB42590',col:'#0369a1'};"
"return {c:'astm',t:'ASTM',col:'#64748b'};}"
// 4 格信号柱：按 RSSI 分级着色（强绿 / 中黄 / 弱红）
"function sigBar(r){"
"const lv=r>=-55?4:(r>=-67?3:(r>=-78?2:(r>=-90?1:0)));"
"const cls=lv>=3?'':(lv===2?'w':'weak');"
"let h=`<div class=\"sig\" title=\"${r} dBm\">`;"
"for(let i=1;i<=4;i++){const on=i<=lv;"
"h+=`<i class=\"${on?'on '+(lv>0?cls:''):''}\" style=\"height:${4+i*2.5}px\"></i>`;}"
"return h+'</div>';}"
"function cell(v,unit,label,cls){"
"return `<div class=\"${cls||''}\"><b>${v}${unit?`<small>${unit}</small>`:''}</b><span>${label}</span></div>`;}"
// ---- 导航到目标位置：直接调起手机地图 App（高德 / 百度），不是打开网页 ----
// 用各家原生 URL Scheme：高德 amapuri://route/plan（iOS: iosamap://path）、
// 百度 bdapp://map/direction（iOS: baidumap://map/direction）。
// 坐标仍传 GNSS 原始 WGS-84：高德 dev=1 = 需要国测加密（由 App 自己转 GCJ-02）；
// 百度带 coord_type=wgs84 同理。App 没装（1.5 秒内没跳走）才降级到网页版。
"function posOk(la,lo){la=Number(la);lo=Number(lo);"
"return isFinite(la)&&isFinite(lo)&&Math.abs(la)<=90&&Math.abs(lo)<=180"
"&&!(Math.abs(la)<1e-6&&Math.abs(lo)<1e-6);}"   // 0,0 视为未上报
"function isIOS(){return /iPad|iPhone|iPod/.test(navigator.userAgent||'');}"
"function navDestName(kind,nm){"
"const tag=(kind==='op')?'飞手':'无人机';"
"const base=(nm?nm:tag);"
"return base.slice(0,40);}"
// 原生 App 的 scheme。alt=1 时换用各家的备用协议头（百度 Android 上
// bdapp:// 与 baidumap:// 都有人注册，两个都试能明显提高唤起成功率）
"function navScheme(kind,la,lo,nm,prov,alt){"
"la=Number(la);lo=Number(lo);"
"const dn=encodeURIComponent(navDestName(kind,nm));"
"if(prov==='bd'){"
"const dest='latlng:'+la.toFixed(6)+','+lo.toFixed(6)+'|name:'+dn;"
"const ios=isIOS();"
"const head=alt?(ios?'bdapp://map/direction?':'baidumap://map/direction?')"
":(ios?'baidumap://map/direction?':'bdapp://map/direction?');"
"return head+'destination='+dest+"
"'&mode=driving&coord_type=wgs84&src=webapp.crid.nav';}"
"return (isIOS()?'iosamap://path?':'amapuri://route/plan/?')"
"+'sourceApplication=crid&dlat='+la.toFixed(6)+'&dlon='+lo.toFixed(6)"
"+'&dname='+dn+'&dev=1&t=0';}"
"function navSchemes(kind,la,lo,nm,prov){"
"const a=navScheme(kind,la,lo,nm,prov,0);"
"if(prov==='bd')return [a,navScheme(kind,la,lo,nm,prov,1)];"
"return [a];}"
// 网页版兜底（App 未安装时）
"function navUrl(kind,la,lo,nm,prov){"
"la=Number(la);lo=Number(lo);"
"const name=navDestName(kind,nm);"
"if(prov==='bd'){"
"return 'https://api.map.baidu.com/mobile/direction?destination=latlng:'"
"+la.toFixed(6)+','+lo.toFixed(6)+'|name:'+encodeURIComponent(name)"
"+'&mode=driving&output=html&coord_type=wgs84&src=webapp.crid.nav';}"
"return 'https://uri.amap.com/navigation?to='+lo.toFixed(6)+','+la.toFixed(6)+','"
"+encodeURIComponent(name)+'&mode=car&policy=1&coordinate=wgs84&src=crid';}"
// 唤起策略：逐个尝试 scheme（每个给 900ms），全部失败才降级网页版。
// 只要页面被切到后台（App 起来了）就立刻停止后续尝试。
"function navTo(kind,la,lo,nm,prov){"
"if(!posOk(la,lo)){msg('该目标暂未上报有效坐标，无法导航',false);return;}"
"const list=navSchemes(kind,la,lo,nm,prov);"
"const web=navUrl(kind,la,lo,nm,prov);"
"let i=0,jumped=false,tm=null;"
"const stop=function(){jumped=true;if(tm){clearTimeout(tm);tm=null;}"
"document.removeEventListener('visibilitychange',stop);"
"window.removeEventListener('blur',stop);"
"window.removeEventListener('pagehide',stop);};"
"document.addEventListener('visibilitychange',stop);"
"window.addEventListener('blur',stop);"
"window.addEventListener('pagehide',stop);"
"const next=function(){"
"if(jumped)return;"
"if(i>=list.length){"
"if(document.visibilityState!=='hidden'){try{location.href=web;}catch(e){}}"
"return;}"
"try{location.href=list[i++];}catch(e){}"
"tm=setTimeout(next,900);};"
"next();}"
// 点按钮先弹地图选择层（高德/百度二选一），选完才跳转，页面不常驻多余控件
"const NP={kind:'drone',la:0,lo:0,nm:''};"
"function navClose(){const m=q('navpick');if(m)m.classList.remove('on');}"
"function navPick(kind,la,lo,nm){"
"if(!posOk(la,lo)){msg('该目标暂未上报有效坐标，无法导航',false);return;}"
"NP.kind=kind;NP.la=Number(la);NP.lo=Number(lo);NP.nm=nm||'';"
"const t=q('navpick_t');if(t)t.textContent='导航到'+((kind==='op')?'飞手位置':'无人机位置');"
"const p=q('navpick_p');"
"if(p)p.textContent=navDestName(kind,nm)+' · '+NP.la.toFixed(6)+', '+NP.lo.toFixed(6)+'（WGS-84）';"
"const m=q('navpick');if(m)m.classList.add('on');}"
"function navGo(prov){navClose();"
"navTo(NP.kind,NP.la,NP.lo,NP.nm,prov);}"
"function navBtn(kind,la,lo,nm){"
"const tag=(kind==='op')?'飞手':'无人机';"
"return '<button type=\"button\" class=\"navbtn\" data-nav=\"'+kind+'\"'"
"+' data-la=\"'+Number(la).toFixed(6)+'\" data-lo=\"'+Number(lo).toFixed(6)+'\"'"
"+' data-nm=\"'+esc(nm||'')+'\">导航到'+tag+'</button>';}"
// ---- 报文详情折叠：枚举值文本表（国标与 ASTM 兼容枚举） ----
"const OPT=['实时位置','起飞点位置','自定义位置','未知'];"
"const AGLT=['起飞点','地面','自定义','实时 GNSS'];"
"const HACC=['未知','<18.5 km','<7.4 km','<3.7 km','<1.85 km','<926 m','<556 m','<185 m',"
"'<93 m','<30 m','<10 m','<3 m','<1 m'];"
"const VACC=['未知','<150 m','<45 m','<25 m','<10 m','<3 m','<1 m'];"
"const SACC=['未知','<10 m/s','<3 m/s','<1 m/s','<0.3 m/s'];"
"const TACC=['未知','<=0.2 s','<=0.5 s','<=1 s','<=2 s','<=5 s','<=10 s','<=20 s',"
"'<=30 s','<=60 s','>60 s'];"
// 折叠状态按 mac 记忆：轮询每 2 秒重建 DOM 后恢复各卡展开态
"const s_fold={};"
"function updFoldBtn(){const b=q('foldall');if(!b)return;"
"const cs=document.querySelectorAll('#rxlist .dcard');"
"const allOpen=cs.length>0&&Array.prototype.every.call(cs,function(c){return c.classList.contains('open');});"
"b.textContent=allOpen?'全部折叠 ▴':'全部展开 ▾';}"
"function foldAll(){const cs=document.querySelectorAll('#rxlist .dcard');"
"const anyClosed=Array.prototype.some.call(cs,function(c){return !c.classList.contains('open');});"
"Array.prototype.forEach.call(cs,function(c){c.classList.toggle('open',anyClosed);"
"s_fold[c.dataset.mac]=anyClosed;});updFoldBtn();}"
// 详情块：报文携带的全部数据项（括号内为国标数据项编号）。
// 卡片折叠态只留关键几项，其余全部收在这里——操作员 ID、MAC、机型分类、
// 精度、时间戳、链路统计一项不少，保证「收到的信息都能查到」。
"function dtlHtml(t){"
"const p=rxProto(t.protocol);"
"const rows=[];const row=function(k,v){if(v!==undefined&&v!==null&&v!=='')rows.push("
"'<tr><td>'+k+'</td><td>'+v+'</td></tr>');};"
"const txt=function(k,v){row(k,(v===undefined||v===null||v==='')?undefined:esc(v));};"
// ---- 身份信息 ----
"txt('MAC 地址',t.mac);"
"txt('UAS ID (序列号)',t.uas_id);"
"txt('机型 / 描述',t.drone_name);"
"row('标准 / 协议',p.t+' · 协议号 '+t.protocol);"
"row('机型分类',t.protocol===4"
"?('新国标分类 '+t.ua_type+' · '+(GBCN[t.ua_type]||'未定义'))"
":('机型代码 '+t.ua_type+' · '+(UATN[t.ua_type]||'未定义')));"
"txt('第二识别码',t.uas_id2);"
"txt('操作员 ID',t.operator_id);"
"txt('实名登记号',t.realname_id);"
"row('运行状态',RXST[t.status]||('状态 '+t.status));"
// ---- 位置与运动 ----
"row('纬度',f6(t.latitude));"
"row('经度',f6(t.longitude));"
"row('大地高度 (013)',f1(t.altitude_geo)+' m');"
"row('相对高度 (011)',t.altitude_agl===undefined?undefined:"
"f1(t.altitude_agl)+' m'+(t.agl_type!==undefined?' · '+AGLT[t.agl_type]:''));"
"row('气压高度 (014)',t.altitude_baro===undefined?undefined:f1(t.altitude_baro)+' m');"
"row('水平速度',f1(t.speed_horizontal)+' m/s');"
"row('垂直速度 (012)',f1(t.speed_vertical)+' m/s');"
"row('航向',f1(t.direction)+' °');"
"if(t.has_op_loc){"
"row('飞手纬度',f6(t.op_latitude));"
"row('飞手经度',f6(t.op_longitude));"
"row('飞手海拔 (007)',f1(t.op_altitude_geo)+' m');"
"}"
// ---- 精度与时间戳 ----
"if(t.protocol===4){"
"row('遥控站位置类型 (005)',OPT[t.op_loc_type!==undefined?t.op_loc_type:3]);"
"row('坐标系 (016)',t.coord_sys===undefined?undefined:"
"(t.coord_sys===0?'WGS-84':'类型 '+t.coord_sys));"
"row('水平精度 (017)',t.h_acc===undefined?undefined:(HACC[t.h_acc]||('枚举 '+t.h_acc)));"
"row('垂直精度 (018)',t.v_acc===undefined?undefined:(VACC[t.v_acc]||('枚举 '+t.v_acc)));"
"row('速度精度 (019)',t.spd_acc===undefined?undefined:(SACC[t.spd_acc]||('枚举 '+t.spd_acc)));"
"row('报文时间 (020)',t.ts_unix_ms===undefined?undefined:"
"new Date(t.ts_unix_ms).toLocaleTimeString());"
"row('时间戳精度 (021)',t.ts_acc===undefined?undefined:(TACC[t.ts_acc]||('枚举 '+t.ts_acc)));"
"}else{"
"row('报文时间 (相对)',t.ts_rel_10s===undefined?undefined:f1(t.ts_rel_10s/10)+' s');"
"row('水平精度',t.h_acc===undefined?undefined:(HACC[t.h_acc]||('枚举 '+t.h_acc)));"
"row('垂直精度',t.v_acc===undefined?undefined:(VACC[t.v_acc]||('枚举 '+t.v_acc)));"
"row('速度精度',t.spd_acc===undefined?undefined:(SACC[t.spd_acc]||('枚举 '+t.spd_acc)));"
"row('高度基准',t.agl_type===undefined?undefined:(AGLT[t.agl_type]||('类型 '+t.agl_type)));"
"}"
// ---- 链路与统计 ----
"row('信道',t.channel);"
"row('信号强度 (RSSI)',t.rssi===undefined?undefined:t.rssi+' dBm');"
"row('累计帧数',t.msg_count===undefined?undefined:t.msg_count+' 帧');"
"row('最后收到',t.age_ms===undefined?undefined:ageTxt(t.age_ms));"
"row('在线状态',t.active===false?'已离线（10 秒无信号，保留 30 分钟）':'在线');"
"row('已观测时长',t.first_age_ms===undefined?undefined:ageTxt(t.first_age_ms));"
"if(!rows.length)return '';"
"return '<div class=\"dtl\"><div class=\"dtl-h\">报文详情 ('+rows.length+' 项)<i>▶</i></div>'"
"+'<div class=\"dtl-b\"><table class=\"dtab\">'+rows.join('')+'</table></div></div>';}"
"function rxCard(t){"
"const p=rxProto(t.protocol);"
"const el=document.createElement('div');"
"el.className='dcard'+(t.active===false?' off':'');"
"el.dataset.mac=t.mac;"
"const stTxt=RXST[t.status]||('状态'+t.status);"
"const air=(t.status===2);"
// 机型描述：新国标无 Self-ID 数据项，用分类兜底
"let name=t.drone_name||'';"
"if(!name)name=(t.protocol===4)?('新国标 '+(GBCN[t.ua_type]||('分类'+t.ua_type))):'未广播机型名';"
// 链路与统计并入机型标题行右侧：MAC · 信号 · 帧数 · 最后收到
"const lnk=esc(t.mac)+' · '+t.rssi+' dBm · '+t.msg_count+' 帧 · '+ageTxt(t.age_ms);"
"let h=`<div class=\"dh\"><span class=\"id\" title=\"${esc(t.uas_id)}\">${esc(t.uas_id||'(无 UAS ID)')}</span>`"
"+`<span class=\"tag ${p.c}\">${p.t}</span>`"
"+`<span class=\"tag ${air?'air':'pend'}\">${esc(stTxt)}</span>`"
"+sigBar(t.rssi)+`</div><div class=\"db\">`;"
"h+=`<div class=\"dname\"><span class=\"nm\" title=\"${esc(name)}\">${esc(name)}</span>`"
"+`<span class=\"lnk\" title=\"链路与统计：MAC · 信号强度 · 累计帧数 · 最后收到\">${lnk}</span></div>`;"
"h+=`<div class=\"kv\">`"
"+cell(f6(t.latitude),'','纬度','hl')"
"+cell(f6(t.longitude),'','经度','hl')"
"+cell(f1(t.altitude_geo),'m','大地高度')"
"+cell(f1(t.speed_horizontal),'m/s','水平速度')"
"+cell(f1(t.direction),'°','航向')"
"+cell(f1(t.speed_vertical),'m/s','垂直速度')"
"+`</div>`;"
// 副信息行：飞手位置与实名登记号同一行；操作员 ID 收进报文详情，不再占版面
"const idt=[];"
"if(t.has_op_loc)idt.push([f6(t.op_latitude)+', '+f6(t.op_longitude),'飞手位置 (遥控站)']);"
"if(t.realname_id)idt.push([esc(t.realname_id),'实名登记号']);"
"if(idt.length){h+='<div class=\"kv2\">';"
"idt.forEach(function(x){"
"h+='<div'+(idt.length===1?' class=\"wide\"':'')+'><b>'+x[0]+'</b><span>'+x[1]+'</span></div>';});"
"h+='</div>';}"
// 导航按钮：分别导航到无人机当前位置 / 飞手（遥控站）位置，无有效坐标则不显示。
// 目的地名称用机型名（如 DJI Air 3S）而不是 20 位序列号——序列号在地图 App 里
// 只会显示成一串乱码，用户看不出这是哪架无人机
"let nb='';"
"if(posOk(t.latitude,t.longitude))nb+=navBtn('drone',t.latitude,t.longitude,t.drone_name);"
"if(t.has_op_loc&&posOk(t.op_latitude,t.op_longitude))"
"nb+=navBtn('op',t.op_latitude,t.op_longitude,'');"
"if(nb)h+='<div class=\"navrow\">'+nb+'</div>';"
"h+=dtlHtml(t)+'</div>';"
"el.innerHTML=h;"
// 折叠头点击：切换展开态（阻断冒泡，不触发卡片本身的地图聚焦）
"const dh=el.querySelector('.dtl-h');"
"if(dh){dh.onclick=function(ev){ev.stopPropagation();"
"el.classList.toggle('open');s_fold[t.mac]=el.classList.contains('open');updFoldBtn();};}"
// 导航按钮：必须阻断冒泡，否则会连带触发下面卡片的地图聚焦
"Array.prototype.forEach.call(el.querySelectorAll('.navbtn'),function(b){"
"b.onclick=function(ev){ev.stopPropagation();"
"navPick(b.dataset.nav,b.dataset.la,b.dataset.lo,b.dataset.nm);};});"
// 点击卡片聚焦地图：仅调用已加载的独立模块，未加载或报错都不影响卡片本身
"el.onclick=function(){try{if(window.RXMAP)RXMAP.focus(t.mac);}catch(e){}};"
"return el;}"
""
"async function pollRx(){"
"try{"
"const r=await fetch('/api/rx');const j=await r.json();"
"try{if(window.RXMAP)RXMAP.update(j.tracks);}catch(e){}"   // 地图刷新独立保护
"const s=j.stats;"
// 在线/离线分组：active=false 为离线（后端 10 秒无信号标记，保留 30 分钟后清除）
"const on=[],off=[];"
"j.tracks.forEach(function(t){(t.active===false?off:on).push(t);});"
"on.sort(function(a,b){return b.rssi-a.rssi;});"
"off.sort(function(a,b){return a.age_ms-b.age_ms;});"
"q('s_online').textContent=on.length;"
"q('s_rid').textContent=s.rid;"
"q('s_total').textContent=s.total;"
"q('s_ok').textContent=s.ok;"
"q('s_ovf').textContent=s.ovf;"
"q('s_ovfbox').className='chip'+((s.ovf>0)?' warn':'');"
"let fresh=0;"
"on.forEach(function(t){if(t.age_ms<3000)fresh++;});"
"q('rxstate').innerHTML=(fresh>0?`<b style=\"color:#0a7d43\">${fresh} 个目标</b>信号良好（3 秒内）`"
":'暂未收到 RID 帧，持续监听中…')+` &nbsp;|&nbsp; 更新于 ${new Date().toLocaleTimeString()}`;"
// 列表分区：在线在上（按信号强度），已离线灰度在下（按最后收到时间）
"const box=q('rxlist');"
"box.innerHTML='';"
"if(!on.length&&!off.length){"
"box.innerHTML='<div class=\"rxb0\"><b>◎</b>尚未发现无人机 RID 广播<br>周边设备需与本机处于同一 Wi-Fi 信道</div>';"
"return;}"
"function sec(txt,dim){const d=document.createElement('div');"
"d.className='rxsec'+(dim?' dim':'');d.textContent=txt;box.appendChild(d);}"
"if(on.length){sec('在线信号 · '+on.length,false);"
"on.forEach(function(t){box.appendChild(rxCard(t));});}"
"if(off.length){sec('已离线 · '+off.length+'（10 秒无信号，保留 30 分钟）',true);"
"off.forEach(function(t){box.appendChild(rxCard(t));});}"
// 轮询重建 DOM 后：恢复各卡展开态 + 同步折叠按钮文字
"Array.prototype.forEach.call(box.querySelectorAll('.dcard'),function(c){"
"if(s_fold[c.dataset.mac])c.classList.add('open');});"
"updFoldBtn();"
"}catch(e){q('rxstate').textContent='轮询失败：'+e;}}"
"q('foldall').onclick=foldAll;"
// 导航选择层：选项按钮与取消/点遮罩关闭，只在弹层内有效，不影响页面其它点击
"(function(){"
"const m=q('navpick');if(!m)return;"
"m.onclick=function(ev){if(ev.target===m)navClose();};"
"Array.prototype.forEach.call(m.querySelectorAll('.navopt'),function(b){"
"b.onclick=function(ev){ev.stopPropagation();navGo(b.getAttribute('data-prov'));};});"
"const c=q('navpick_c');if(c)c.onclick=navClose;"
"})();"
// ---- 在线地图（Leaflet）与离线网格双模式 ----
// map/opMarker/circle 等变量已在脚本开头统一声明
"let gbase=[0,0],gpick=null;"
"function dotIcon(c){return L.divIcon({className:'',"
"html:'<div style=\"width:14px;height:14px;border-radius:50%;background:'+c+"
"';border:2px solid #fff;box-shadow:0 1px 3px rgba(0,0,0,.4)\"></div>',"
"iconSize:[14,14],iconAnchor:[7,7]});}"
"function drawCircle(){if(!map)return;if(circle){map.removeLayer(circle);circle=null;}"
"const r=(parseFloat(fld('patrol_radius_lat').value)||0)*111320;"
"if(r>0&&dMarker){circle=L.circle(dMarker.getLatLng(),{radius:r,color:'#0a7d43',"
"weight:1,fillOpacity:.05}).addTo(map);}}"
"function initMap(){"
"if(ginit){syncMarkers();return;}"
"ginit=true;const o=opLL(),d=baseLL();"
"if(typeof L!=='undefined'&&!window._mapfail){"
"map=L.map('map').setView(o,15);"                     // 地图默认以飞手位置为中心
"L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',"
"{maxZoom:19,attribution:'&copy; OpenStreetMap'}).addTo(map);"
"opMarker=L.marker(o,{draggable:true,icon:dotIcon('#c0392b')}).addTo(map);"
"dMarker=L.marker(d,{draggable:true,icon:dotIcon('#0a7d43')}).addTo(map);"
"opMarker.on('dragend',function(e){const p=opMarker.getLatLng();"
"applyPick('op',p.lat,p.lng);});"
"dMarker.on('dragend',function(e){const p=dMarker.getLatLng();"
"applyPick('drone',p.lat,p.lng);});"
"map.on('click',function(e){applyPick(ptarget(),e.latlng.lat,e.latlng.lng);});"
"live=L.circleMarker(d,{radius:6,color:'#1565c0',fillColor:'#1565c0',"
"fillOpacity:.9}).addTo(map);"
"drawCircle();}"
"else{document.getElementById('map').style.display='none';"
"document.getElementById('gridbox').style.display='block';"
"gbase=o;drawGrid();"                                  // 离线网格同样以飞手为中心
"document.getElementById('mpp').onchange=function(){drawGrid();};"
"const cv=document.getElementById('grid');"
"cv.addEventListener('click',function(e){"
"const rc=cv.getBoundingClientRect();const s=320/rc.width;"
"const x=(e.clientX-rc.left)*s,y=(e.clientY-rc.top)*s;"
"const mpp=parseFloat(fld('mpp').value);"
"const dx=(x-160)*mpp,dy=(160-y)*mpp;"
"const la=gbase[0]+dy/111320;"
"const lo=gbase[1]+dx/(111320*Math.cos(gbase[0]*Math.PI/180));"
"applyPick(ptarget(),la,lo);gpick=[la,lo];drawGrid();});}}"
"function drawGrid(){const cv=document.getElementById('grid');"
"if(!cv)return;"
"const x=cv.getContext('2d');"
"x.fillStyle='#fafafa';x.fillRect(0,0,320,320);"
"x.strokeStyle='#e2e2e2';"
"for(let i=0;i<=320;i+=40){x.beginPath();x.moveTo(i,0);x.lineTo(i,320);x.stroke();"
"x.beginPath();x.moveTo(0,i);x.lineTo(320,i);x.stroke();}"
"x.strokeStyle='#999';x.beginPath();x.moveTo(160,0);x.lineTo(160,320);"
"x.moveTo(0,160);x.lineTo(320,160);x.stroke();"
"x.fillStyle='#555';x.font='11px sans-serif';"
"x.fillText(fld('mpp').value+'米/像素',8,312);"
"x.fillStyle='#0a7d43';x.beginPath();x.arc(160,160,6,0,6.3);x.fill();"
"x.fillStyle='#0a7d43';x.fillText('中心=基准点',170,164);"
// 飞行轨迹（蓝线）：与在线地图同数据源，网格模式下同样可见
"if(trailPts.length>1){const tm=parseFloat(fld('mpp').value);"
"x.strokeStyle='rgba(21,101,192,.55)';x.lineWidth=2;x.beginPath();"
"trailPts.forEach(function(p,i){"
"const px=160+(p[1]-gbase[1])*111320*Math.cos(gbase[0]*Math.PI/180)/tm;"
"const py=160-(p[0]-gbase[0])*111320/tm;"
"if(i)x.lineTo(px,py);else x.moveTo(px,py);});x.stroke();x.lineWidth=1;}"
"if(gpick){const mpp=parseFloat(fld('mpp').value);"
"const px=160+(gpick[1]-gbase[1])*111320*Math.cos(gbase[0]*Math.PI/180)/mpp;"
"const py=160-(gpick[0]-gbase[0])*111320/mpp;"
"x.fillStyle='#c0392b';x.beginPath();x.arc(px,py,6,0,6.3);x.fill();}}"
// ---- 发射端飞行轨迹：每 2 秒把实时广播位置追加进轨迹 ----
// 与接收端航迹（独立 /rxmap.js 模块内实现）互不相干
"function trailPush(la,lo){const n=trailPts.length;"
"if(n){const dla=la-trailPts[n-1][0],dlo=lo-trailPts[n-1][1];"
"if(Math.abs(dla)>0.005||Math.abs(dlo)>0.005){trailPts=[];}"   // 位置跳变（改基准点/保存应用）后重新起线
"else if(Math.abs(dla)>1e-7||Math.abs(dlo)>1e-7)trailPts.push([la,lo]);}"
"else{trailPts.push([la,lo]);}"
"if(trailPts.length>240)trailPts.shift();"   // 保留最近 240 点（约 8 分钟巡游）
"if(map){if(!trailLine)trailLine=L.polyline(trailPts,"
"{color:'#1565c0',weight:2,opacity:.6}).addTo(map);"
"else trailLine.setLatLngs(trailPts);}}"
// ---- 实时广播位置轮询 ----
"async function poll(){try{const r=await fetch('/api/status');const j=await r.json();"
"const t='实时广播位置: '+j.latitude.toFixed(6)+', '+j.longitude.toFixed(6)+"
"'  高度 '+j.altitude_msl.toFixed(1)+'m  航向 '+j.heading.toFixed(0)+'°';"
"document.getElementById('liveinfo').textContent=t;"
"if(live){live.setLatLng([j.latitude,j.longitude]);}"
"trailPush(j.latitude,j.longitude);}"
"catch(e){}"
"if(Date.now()-sT>600000)syncTime();}"
// ---- 设备授时：GB46750 数据项 020 需要真实 Unix 毫秒，设备本身无 RTC/不联网 ----
"var sT=0;"
"function syncTime(){sT=Date.now();try{fetch('/api/time',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch_ms:sT})})"
".catch(function(){});}catch(e){}}"
"setInterval(poll,2000);"
"syncTime();"
"load();"
"</script></body></html>";

// ================================================================
// URI 处理
// ================================================================

static esp_err_t handler_page(httpd_req_t *req) {
    // gzip 优先：页面 36KB 直传在 softAP 上偶发截断（手机端脚本解析失败导致
    // 整页空白），压缩后仅 12KB，大幅降低传输失败概率
    char ae[64] = {0};
    bool gzip_ok = false;
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof(ae)) == ESP_OK) {
        gzip_ok = strstr(ae, "gzip") != NULL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // 禁止缓存：页面随固件更新，手机连热点后必须拿到当前版本
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    if (gzip_ok) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)PAGE_HTML_GZ, PAGE_HTML_GZ_LEN);
    }
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

// 接收端地图脚本独立下发：与主页面分离，按需加载。加载失败只影响地图，
// 主页面与接收列表不受任何影响（这是本次重做的核心隔离原则）。
static esp_err_t handler_rxmapjs(httpd_req_t *req) {
    char ae[64] = {0};
    bool gzip_ok = false;
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof(ae)) == ESP_OK) {
        gzip_ok = strstr(ae, "gzip") != NULL;
    }
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    if (gzip_ok) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)RXMAP_JS_GZ, RXMAP_JS_GZ_LEN);
    }
    return httpd_resp_send(req, RXMAP_JS, HTTPD_RESP_USE_STRLEN);
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
    cJSON_AddNumberToObject(j, "ua_type", cfg->ua_type);
    cJSON_AddNumberToObject(j, "base_latitude", cfg->base_latitude);
    cJSON_AddNumberToObject(j, "base_longitude", cfg->base_longitude);
    cJSON_AddNumberToObject(j, "base_altitude_msl", cfg->base_altitude_msl);
    cJSON_AddNumberToObject(j, "operator_latitude", cfg->operator_lat);
    cJSON_AddNumberToObject(j, "operator_longitude", cfg->operator_lon);
    cJSON_AddNumberToObject(j, "operator_altitude", cfg->operator_alt);
    cJSON_AddNumberToObject(j, "altitude_agl", cfg->altitude_agl);
    cJSON_AddNumberToObject(j, "speed_horizontal", cfg->speed_horizontal);
    cJSON_AddNumberToObject(j, "status", cfg->status);
    cJSON_AddNumberToObject(j, "patrol_speed", cfg->patrol_speed);
    cJSON_AddNumberToObject(j, "patrol_radius_lat", cfg->patrol_radius_lat);
    cJSON_AddNumberToObject(j, "patrol_radius_lon", cfg->patrol_radius_lon);
    cJSON_AddNumberToObject(j, "device_mode", cfg->device_mode);
    cJSON_AddNumberToObject(j, "rid_standard", cfg->rid_standard);
    cJSON_AddStringToObject(j, "realname_id", cfg->realname_id);
    cJSON_AddNumberToObject(j, "ua_category_gb", cfg->ua_category_gb);
    cJSON_AddNumberToObject(j, "band", cfg->band);
    cJSON_AddNumberToObject(j, "tx_2g", cfg->tx_2g);
    cJSON_AddNumberToObject(j, "rx_2g", cfg->rx_2g);
    cJSON_AddNumberToObject(j, "tx_5g", cfg->tx_5g);
    cJSON_AddNumberToObject(j, "rx_5g", cfg->rx_5g);
    cJSON_AddNumberToObject(j, "channel_5g", cfg->channel_5g);
    cJSON_AddBoolToObject(j, "has5g", CRID_HAS_5G_BAND ? true : false);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 从 JSON 取浮点/整数（不存在则跳过）。
// 兼容数字与字符串两种类型：浏览器表单提交的值常为字符串（"23.14"）。
static bool jget_num(const cJSON *j, const char *key, double *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(j, key);
    if (it == NULL) return false;
    if (cJSON_IsNumber(it)) {
        *out = cJSON_GetNumberValue(it);
        return true;
    }
    if (cJSON_IsString(it)) {
        const char *s = cJSON_GetStringValue(it);
        if (s != NULL && *s != '\0') {
            char *end = NULL;
            double v = strtod(s, &end);
            if (end != s) { *out = v; return true; }
        }
    }
    return false;
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
        if (v >= 1 && v <= 13) next.channel = (uint8_t)v;
        // 顺手接受 5.8G 信道号，写入独立字段，避免 149 被判成非法
        else if (v >= CRID_5G_CH_MIN && v <= CRID_5G_CH_MAX) next.channel_5g = (uint8_t)v;
        else snprintf(err, sizeof(err), "信道须在 1-13 或 149-165");
    }
    if (err[0] == '\0' && jget_num(j, "beacon_interval_ms", &v)) {
        if (v < 200 || v > 60000) snprintf(err, sizeof(err), "广播间隔须在 200-60000ms");
        else next.beacon_interval_ms = (uint16_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "ua_type", &v)) {
        if (v < 0 || v > 15) snprintf(err, sizeof(err), "机型代码须在 0-15");
        else next.ua_type = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_latitude", &v)) {
        if (v < -90 || v > 90) snprintf(err, sizeof(err), "纬度须在 -90~90");
        else next.base_latitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_longitude", &v)) {
        if (v < -180 || v > 180) snprintf(err, sizeof(err), "经度须在 -180~180");
        else next.base_longitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_latitude", &v)) {
        if (v < -90 || v > 90) snprintf(err, sizeof(err), "飞手纬度须在 -90~90");
        else next.operator_lat = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_longitude", &v)) {
        if (v < -180 || v > 180) snprintf(err, sizeof(err), "飞手经度须在 -180~180");
        else next.operator_lon = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_altitude", &v)) {
        if (v < -1000 || v > 10000) snprintf(err, sizeof(err), "飞手海拔须在 -1000~10000m");
        else next.operator_alt = (float)v;
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
    // 频段与收发开关。老页面只传 device_mode，这里做兼容映射到 2.4G 组。
    bool has_switch_fields = (cJSON_GetObjectItemCaseSensitive(j, "band") != NULL);
    if (err[0] == '\0' && has_switch_fields) {
        if (jget_num(j, "band", &v)) {
            if (v < 0 || v > 1) snprintf(err, sizeof(err), "频段取值 0-1");
            else next.band = (uint8_t)v;
        }
        if (jget_num(j, "tx_2g", &v)) next.tx_2g = v ? 1 : 0;
        if (jget_num(j, "rx_2g", &v)) next.rx_2g = v ? 1 : 0;
        if (jget_num(j, "tx_5g", &v)) next.tx_5g = v ? 1 : 0;
        if (jget_num(j, "rx_5g", &v)) next.rx_5g = v ? 1 : 0;
        if (jget_num(j, "channel_5g", &v)) {
            if (v < CRID_5G_CH_MIN || v > CRID_5G_CH_MAX) {
                snprintf(err, sizeof(err), "5.8G 信道须在 149-165");
            } else {
                next.channel_5g = (uint8_t)v;
            }
        }
#if !CRID_HAS_5G_BAND
        // 无 5G 射频的芯片：无论页面传什么都强制回落 2.4G
        if (next.band == CRID_BAND_5G) next.band = CRID_BAND_2G;
#endif
        // 单射频互斥：老页面或手工请求若把发射与接收都置 1，这里归一化成二选一
        crid_config_radio_mutex(&next);
    } else if (err[0] == '\0' && jget_num(j, "device_mode", &v)) {
        if (v < 0 || v > 1) snprintf(err, sizeof(err), "模式取值 0-1");
        else {
            next.device_mode = (uint8_t)v;
            next.band = CRID_BAND_2G;
            next.tx_2g = (v == DEVICE_MODE_TX) ? 1 : 0;
            next.rx_2g = (v == DEVICE_MODE_RX) ? 1 : 0;
        }
    }
    if (err[0] == '\0' && jget_num(j, "rid_standard", &v)) {
        if (v < 0 || v > 1) snprintf(err, sizeof(err), "标准取值 0-1");
        else next.rid_standard = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "ua_category_gb", &v)) {
        if (v < 0 || v > 4) snprintf(err, sizeof(err), "新国标分类取值 0-4");
        else next.ua_category_gb = (uint8_t)v;
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "realname_id")) != NULL && cJSON_IsString(item)) {
        // 仅允许数字与大写字母（实名登记号后 8 位格式），空 = 不填
        const char *s = cJSON_GetStringValue(item);
        size_t n = strlen(s);
        if (n > 8) { snprintf(err, sizeof(err), "实名登记号最多 8 位"); }
        else {
            for (size_t i = 0; i < n; i++) {
                char c = s[i];
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                    snprintf(err, sizeof(err), "实名登记号仅限字母数字");
                    break;
                }
            }
            if (err[0] == '\0') {
                memset(next.realname_id, 0, sizeof(next.realname_id));
                for (size_t i = 0; i < n; i++) {
                    next.realname_id[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
                }
            }
        }
    }
    cJSON_Delete(j);

    if (err[0] != '\0') {
        httpd_resp_set_type(req, "application/json");
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", err);
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    // 校验通过，应用配置
    uint8_t old_ch = crid_band_channel(cfg);
    uint8_t new_ch = crid_band_channel(&next);
    bool band_changed = (next.band != cfg->band);
    bool ap_changed = (strcmp(next.ssid, cfg->ssid) != 0) ||
                      (new_ch != old_ch) ||
                      (next.beacon_interval_ms != cfg->beacon_interval_ms) ||
                      band_changed;
    bool mode_changed = (next.device_mode != cfg->device_mode) ||
                        (next.tx_2g != cfg->tx_2g) || (next.rx_2g != cfg->rx_2g) ||
                        (next.tx_5g != cfg->tx_5g) || (next.rx_5g != cfg->rx_5g);
    uint8_t saved_counter = cfg->message_counter;   // 计数器不回退
    *cfg = next;
    cfg->message_counter = saved_counter;
    // 同步当前显示位置
    cfg->latitude = cfg->base_latitude;
    cfg->longitude = cfg->base_longitude;
    cfg->altitude_msl = cfg->base_altitude_msl;

    restarted = ap_changed;

    crid_config_save_nvs(cfg);
    ESP_LOGI(TAG, "Config updated via web: id=%s pos=(%.6f,%.6f) mode=%u std=%u restarted=%d",
             cfg->uas_id, cfg->base_latitude, cfg->base_longitude,
             cfg->device_mode, cfg->rid_standard, restarted);

    // 先回包，再动热点。改 SSID / 信道 / 广播间隔会让 Wi-Fi 驱动重建 softAP，
    // 已关联的客户端（就是正在提交表单的这个浏览器）会被踢下线；若在回包前应用，
    // 浏览器拿不到响应，fetch 直接抛 TypeError: Failed to fetch，页面误报"保存失败"。
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, restarted ? "{\"ok\":true,\"restarted\":true}"
                                      : "{\"ok\":true,\"restarted\":false}");

    if (!ap_changed && !mode_changed) {
        return ESP_OK;
    }

    // 给 TCP 栈留出把响应真正发出去的时间，再改热点参数
    vTaskDelay(pdMS_TO_TICKS(300));

    // 接收开启时，换信道/换频段必须先停监听，否则 set_channel 会被正在跑的
    // 混杂接收顶着，新信道不生效
    if (ap_changed && crid_band_rx_enabled(cfg)) {
        crid_rx_stop();
    }

    if (ap_changed) {
        esp_err_t ur;
        if (band_changed) {
            // 单射频：切频段会重建 AP，手机上必然掉线
            ur = crid_wifi_switch_band(cfg->band, cfg->ssid,
                                       crid_band_channel(cfg), cfg->beacon_interval_ms);
            ESP_LOGW(TAG, "频段切换 -> %s, channel=%u",
                     cfg->band == CRID_BAND_5G ? "5.8G" : "2.4G", crid_band_channel(cfg));
        } else {
            ur = crid_wifi_update_ap(cfg->ssid, crid_band_channel(cfg), cfg->beacon_interval_ms);
        }
        if (ur != ESP_OK) {
            ESP_LOGE(TAG, "apply AP failed: %s", esp_err_to_name(ur));
        }
    }

    // 统一按开关重建：发射 IE / 接收监听各自独立
    crid_app_apply_mode();
    return ESP_OK;
}

// 接收监测列表（网页接收模式 2 秒轮询）
static esp_err_t handler_rx(httpd_req_t *req) {
    cJSON *j = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(j, "tracks");
    cJSON *st = cJSON_AddObjectToObject(j, "stats");
    if (j == NULL || arr == NULL || st == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    crid_rx_track_t tracks[CRID_RX_MAX_TRACKS];
    int n = crid_rx_get_tracks(tracks);
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_CreateObject();
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 tracks[i].mac[0], tracks[i].mac[1], tracks[i].mac[2],
                 tracks[i].mac[3], tracks[i].mac[4], tracks[i].mac[5]);
        cJSON_AddStringToObject(t, "mac", mac);
        cJSON_AddBoolToObject(t, "active", tracks[i].active);
        cJSON_AddNumberToObject(t, "rssi", tracks[i].rssi);
        cJSON_AddNumberToObject(t, "channel", tracks[i].channel);
        cJSON_AddNumberToObject(t, "protocol", tracks[i].protocol);
        cJSON_AddStringToObject(t, "uas_id", tracks[i].uas_id);
        cJSON_AddStringToObject(t, "drone_name", tracks[i].drone_name);
        cJSON_AddStringToObject(t, "operator_id", tracks[i].operator_id);
        cJSON_AddStringToObject(t, "realname_id", tracks[i].realname_id);
        cJSON_AddNumberToObject(t, "ua_type", tracks[i].ua_type);
        cJSON_AddNumberToObject(t, "latitude", tracks[i].latitude);
        cJSON_AddNumberToObject(t, "longitude", tracks[i].longitude);
        cJSON_AddNumberToObject(t, "altitude_geo", tracks[i].altitude_geo);
        cJSON_AddNumberToObject(t, "speed_horizontal", tracks[i].speed_horizontal);
        cJSON_AddNumberToObject(t, "speed_vertical", tracks[i].speed_vertical);
        cJSON_AddNumberToObject(t, "direction", tracks[i].direction);
        cJSON_AddNumberToObject(t, "status", tracks[i].status);
        cJSON_AddBoolToObject(t, "has_op_loc", tracks[i].has_op_loc);
        cJSON_AddNumberToObject(t, "op_latitude", tracks[i].op_latitude);
        cJSON_AddNumberToObject(t, "op_longitude", tracks[i].op_longitude);
        cJSON_AddNumberToObject(t, "op_altitude_geo", tracks[i].op_altitude_geo);
        cJSON_AddNumberToObject(t, "msg_count", tracks[i].msg_count);
        cJSON_AddNumberToObject(t, "age_ms", esp_log_timestamp() - tracks[i].last_seen_ms);
        cJSON_AddNumberToObject(t, "first_age_ms",
                                esp_log_timestamp() - tracks[i].first_seen_ms);
        // 补全的报文数据项
        if (tracks[i].uas_id2[0])
            cJSON_AddStringToObject(t, "uas_id2", tracks[i].uas_id2);
        if (tracks[i].has_agl)
            cJSON_AddNumberToObject(t, "altitude_agl", tracks[i].altitude_agl);
        if (tracks[i].has_baro)
            cJSON_AddNumberToObject(t, "altitude_baro", tracks[i].altitude_baro);
        if (tracks[i].has_agl)
            cJSON_AddNumberToObject(t, "agl_type", tracks[i].agl_type);
        if (tracks[i].has_op_loc_type)
            cJSON_AddNumberToObject(t, "op_loc_type", tracks[i].op_loc_type);
        if (tracks[i].has_coord)
            cJSON_AddNumberToObject(t, "coord_sys", tracks[i].coord_sys);
        if (tracks[i].has_acc) {
            cJSON_AddNumberToObject(t, "h_acc", tracks[i].h_acc);
            cJSON_AddNumberToObject(t, "v_acc", tracks[i].v_acc);
            cJSON_AddNumberToObject(t, "spd_acc", tracks[i].spd_acc);
        }
        if (tracks[i].has_ts) {
            cJSON_AddNumberToObject(t, "ts_unix_ms", (double)tracks[i].ts_unix_ms);
            cJSON_AddNumberToObject(t, "ts_acc", tracks[i].ts_acc);
        }
        if (tracks[i].has_ts_rel)
            cJSON_AddNumberToObject(t, "ts_rel_10s", tracks[i].ts_rel_10s);
        cJSON_AddItemToArray(arr, t);
    }

    uint32_t total, rid, ok, ovf;
    crid_rx_get_stats(&total, &rid, &ok, &ovf);
    cJSON_AddNumberToObject(st, "total", total);
    cJSON_AddNumberToObject(st, "rid", rid);
    cJSON_AddNumberToObject(st, "ok", ok);
    cJSON_AddNumberToObject(st, "ovf", ovf);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 实时广播位置（供网页地图显示蓝点，每 2 秒被前端轮询）
static esp_err_t handler_status(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cJSON *j = cJSON_CreateObject();
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(j, "latitude", cfg->latitude);
    cJSON_AddNumberToObject(j, "longitude", cfg->longitude);
    cJSON_AddNumberToObject(j, "altitude_msl", cfg->altitude_msl);
    cJSON_AddNumberToObject(j, "heading", cfg->heading);
    cJSON_AddNumberToObject(j, "speed", cfg->speed_horizontal);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 恢复出厂默认配置（保留消息计数器，防止接收端乱序）
static esp_err_t handler_reset(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cn_crid_config_t next;
    crid_config_init_default(&next);

    bool band_changed = (next.band != cfg->band);
    bool ap_changed = (strcmp(next.ssid, cfg->ssid) != 0) ||
                      (next.channel != cfg->channel) ||
                      (next.beacon_interval_ms != cfg->beacon_interval_ms) ||
                      band_changed;
    uint8_t saved_counter = cfg->message_counter;
    *cfg = next;
    cfg->message_counter = saved_counter;
    cfg->latitude = cfg->base_latitude;
    cfg->longitude = cfg->base_longitude;
    cfg->altitude_msl = cfg->base_altitude_msl;

    bool restarted = ap_changed;
    crid_config_save_nvs(cfg);
    ESP_LOGW(TAG, "Config reset to factory defaults via web (restarted=%d)", restarted);

    // 同 POST /api/config：先回包再改热点，否则客户端在拿到响应前就被踢下线
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, restarted ? "{\"ok\":true,\"restarted\":true}"
                                      : "{\"ok\":true,\"restarted\":false}");
    vTaskDelay(pdMS_TO_TICKS(300));

    if (ap_changed) {
        esp_err_t ur = band_changed
            ? crid_wifi_switch_band(cfg->band, cfg->ssid, crid_band_channel(cfg),
                                    cfg->beacon_interval_ms)
            : crid_wifi_update_ap(cfg->ssid, crid_band_channel(cfg), cfg->beacon_interval_ms);
        if (ur != ESP_OK) {
            ESP_LOGE(TAG, "apply AP failed: %s", esp_err_to_name(ur));
        }
    }
    // 恢复出厂回到 2.4G 发射（init_default 已设定），同步运行时状态
    crid_app_apply_mode();
    return ESP_OK;
}

// 浏览器会自动请求站点图标，返回 204 空响应避免 404 日志刷屏
static esp_err_t handler_favicon(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 浏览器授时：设备无 RTC/不联网，用网页的 Unix 毫秒校准 GB46750 数据项 020
static esp_err_t handler_time(httpd_req_t *req) {
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *j = cJSON_Parse(buf);
    uint64_t ms = 0;
    if (j != NULL) {
        cJSON *it = cJSON_GetObjectItem(j, "epoch_ms");
        if (cJSON_IsNumber(it) && it->valuedouble > 0) {
            ms = (uint64_t)it->valuedouble;
        }
        cJSON_Delete(j);
    }
    crid_gb46750_set_epoch_ms(ms);   // 0 = 清除校准

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ms ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

static const httpd_uri_t uris[] = {
    { .uri = "/",          .method = HTTP_GET,  .handler = handler_page,      .user_ctx = NULL },
    { .uri = "/rxmap.js",  .method = HTTP_GET,  .handler = handler_rxmapjs,   .user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_GET,  .handler = handler_get_config,.user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_POST, .handler = handler_post_config,.user_ctx = NULL },
    { .uri = "/api/status",.method = HTTP_GET,  .handler = handler_status,    .user_ctx = NULL },
    { .uri = "/api/rx",    .method = HTTP_GET,  .handler = handler_rx,        .user_ctx = NULL },
    { .uri = "/api/reset", .method = HTTP_POST, .handler = handler_reset,     .user_ctx = NULL },
    { .uri = "/api/time",  .method = HTTP_POST, .handler = handler_time,      .user_ctx = NULL },
    { .uri = "/favicon.ico",.method = HTTP_GET, .handler = handler_favicon,   .user_ctx = NULL },
};

esp_err_t crid_web_start(cn_crid_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = config;

    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    // 槽位数必须覆盖 uris[] 全部条目（IDF 6.1 默认为 8，路由表已 9 条），
    // 否则超出部分会以 ESP_ERR_HTTPD_HANDLERS_FULL 静默失败、对应接口 404。
    http_cfg.max_uri_handlers = sizeof(uris) / sizeof(uris[0]) + 2;
    http_cfg.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &http_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t r = httpd_register_uri_handler(server, &uris[i]);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "register uri %s failed: %s", uris[i].uri, esp_err_to_name(r));
        }
    }

    ESP_LOGI(TAG, "Web config server started: http://192.168.4.1");
    return ESP_OK;
}
