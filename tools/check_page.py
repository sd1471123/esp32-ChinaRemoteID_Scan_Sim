"""页面校验：提取 PAGE_HTML，做 JS 语法与结构检查。

产出 tools/_page.html、tools/_page.js 供 jsdom_test.js / rx_render_test.js 使用。
"""
import io, os, re, subprocess

SRC = r"E:/esp32-crid-1.1.0/main/crid_web.c"
OUT_DIR = r"E:/esp32-crid-1.1.0/tools"
NODE = r"C:/Users/pc1/.workbuddy/binaries/node/versions/22.22.2-6/node.exe"

src = io.open(SRC, encoding="utf-8").read()


def extract_array(name):
    """扫描 C 源码取出 static const char NAME[] = ...; 内的字符串字面量并反转义。

    逐字符跟踪「是否在字符串内」，遇到字符串外的 ';' 即数组结束，
    这样不会把数组后面的 C 代码（如注释里的引号）误收进来。
    """
    decl = "static const char %s[] =" % name
    i = src.index(decl)
    k = i + len(decl)
    lits, cur, esc = [], None, False
    while k < len(src):
        c = src[k]
        if cur is None:
            if c == '"':
                cur, esc = [], False
            elif c == ';':
                break
        else:
            if esc:
                mapping = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\', '"': '"', "'": "'", '0': '\0'}
                cur.append(mapping.get(c, c))
                esc = False
            elif c == '\\':
                esc = True
            elif c == '"':
                lits.append("".join(cur))
                cur = None
            else:
                cur.append(c)
        k += 1
    if cur is not None:
        raise SystemExit("%s 数组未闭合" % name)
    return "\n".join(lits)   # 换行拼接仅用于可读性，不影响固件


html = extract_array("PAGE_HTML")
io.open(os.path.join(OUT_DIR, "_page.html"), "w", encoding="utf-8", newline="\n").write(html)
print("html len", len(html))

# 提取内联 script 并做语法检查
m = re.search(r'<script>(.*)</script>', html, re.S)
if not m:
    raise SystemExit("未找到内联 script")
js = m.group(1)
io.open(os.path.join(OUT_DIR, "_page.js"), "w", encoding="utf-8", newline="\n").write(js)
r = subprocess.run([NODE, "--check", os.path.join(OUT_DIR, "_page.js")], capture_output=True, text=True)
print("JS check:", r.returncode, r.stderr[-400:])

# 接收端地图脚本：语法 + 隔离性校验（绝不能触碰发射端地图符号）
RXJS = r"E:/esp32-crid-1.1.0/main/rxmap_page.js"
rx_src = io.open(RXJS, encoding="utf-8").read()
r_rx = subprocess.run([NODE, "--check", RXJS], capture_output=True, text=True)
print("rxmap JS check:", r_rx.returncode, r_rx.stderr[-400:])
code_only = "\n".join(l for l in rx_src.splitlines() if not l.strip().startswith(("*", "/*", "//")))
banned = [w for w in ("opMarker", "dMarker", "ginit", "dotIcon", "drawCircle", "initMap",
                      "drawGrid", "syncMarkers", "L.map('map')") if w in code_only]

# div 平衡
tags = re.findall(r'<(/?)div\b[^>]*?(/?)>', html)
depth = 0
for close, selfc in tags:
    if selfc:
        continue
    depth += -1 if close else 1
    if depth < 0:
        print("UNBALANCED at", close)
        break
print("div balance:", depth, "(0=ok), total div tags", len(tags))

# 关键标记：接收端已无地图、卡片与飞手位置保留
checks = {
    "html 闭合 </html>": "</html>" in html,
    "script 标签配对": html.count("<script") == html.count("</script>") == 2,
    "接收端地图容器在": "rxmapbox" in html and "id='rxmap'" in html,
    "地图为独立资源": "/rxmap.js?v=1" in html,
    "接收端地图逻辑不内联": "L.map('rxmap')" not in html and "initRxMap" not in html
                          and "updateRxMap" not in html and "rxFocus" not in html,
    # 页面里对地图模块的调用必须限定在这几个钩子上（含航线回放三个）
    "页面仅通过 RXMAP 钩子调用": all(
        tok in ("RXMAP.boot", "RXMAP.update", "RXMAP.resize", "RXMAP.focus",
                "RXMAP.replay", "RXMAP.replayMark", "RXMAP.replayEnd", "window.RXMAP",
                "RXMAP&&")
        for tok in re.findall(r"RXMAP\.\w+", html)),
    "离线网格容器在": "id='rxgrid'" in html and "id='rxgridnote'" in html,
    "标题已更新": "<title>ESP32-ChinaRemoteID_Scan_Sim</title>" in html
                and "ESP32-ChinaRemoteID<wbr>_Scan_Sim</span></h2>" in html
                and "中国 Remote ID 收发一体固件" in html,
    "接收端无自检代码": "selfCheck" not in html and "__probe" not in html,
    "无 /app.js 与 /api/pageinfo": "/app.js" not in html and "/api/pageinfo" not in html,
    "Leaflet CDN 已恢复": "unpkg.com/leaflet@1.9.4/dist/leaflet.js" in html,
    "卡片渲染保留": "function rxCard(" in html,
    "飞手位置保留": "飞手位置 (遥控站)" in html,
    "轮询保留": "async function pollRx()" in html,
    "保存/恢复初始保留": "async function save()" in html and "async function resetCfg()" in html,
    "地图脚本语法通过": r_rx.returncode == 0,
    "地图脚本零越界": not banned,
    "地图脚本只暴露 RXMAP": rx_src.count("window.RXMAP") == 1,
    # 手机窄屏横向溢出回归：网格子项必须可收缩，长文本必须可断行，
    # 底部按钮必须是等分网格（曾用 flex:1 + flex:0 0 34% 混排，窄屏顶出右边界）
    "窄屏:网格子项可收缩": ".grid>*{min-width:0}" in html,
    "窄屏:长文本可断行": "overflow-wrap:anywhere" in html,
    "窄屏:按钮等分网格": ".actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;min-width:0}" in html,
    "窄屏:无百分比 flex 混排": "flex:0 0 34%" not in html,
    # 高度模型：相对高度由「海拔 − 地面海拔」推导，波动幅度可填 0 固定高度
    "高度:地面海拔输入存在": "id='ground_altitude'" in html,
    "高度:波动幅度输入存在": "id='altitude_wave'" in html,
    "高度:三联动脚本存在": "function altSync(" in html and "altSync('load')" in html,
    "高度:波动说明可固定": "填 0 可固定高度" in html,
    # 新增可自定义参数：气压高度 / 航向 / 垂直速度 / 高度基准 / 遥控站位置类型
    "新增:气压高度输入": "id='baro_altitude'" in html,
    "新增:航向与来源": "id='heading'" in html and "id='heading_mode'" in html,
    "新增:垂直速度与来源": "id='speed_vertical'" in html and "id='vspd_mode'" in html,
    "新增:高度基准": "id='height_type'" in html and "起飞点高度" in html,
    "新增:遥控站位置类型": "id='operator_location_type'" in html and "实时 GNSS" in html,
    "新增:声明项整行": "class='full oldstd'" in html and "class='full newstd'" in html,
    # 四个来源/基准选择必须与其他下拉一致：自定义点击面板，不是原生 select
    "交互:四个下拉都改自定义面板": all("<div class='dd' id='dd%d'" % n for n in (5, 6, 7, 8)),
    "交互:已无原生 select": "<select id='heading_mode'" not in html
                          and "<select id='vspd_mode'" not in html
                          and "<select id='height_type'" not in html
                          and "<select id='operator_location_type'" not in html,
    "交互:下拉走通用构造器": ("function ddMake(" in html
                          and "ddMake('5','heading_mode'" in html
                          and "ddMake('8','operator_location_type'" in html),
    "交互:加载后回填下拉文案": "ddSync('heading_mode');ddSync('vspd_mode')" in html,
    "新增:巡游提示含来源": "不随轨迹切线变化" in html and "不随高度波动变化" in html,
    # 「被其他参数覆盖」这一类坑：手动填数值必须自动切到固定，否则巡游会改回去
    "覆盖:手动输入自动切固定": ("function bindFixVal(" in html
                             and "bindFixVal('heading','heading_mode')" in html
                             and "bindFixVal('speed_vertical','vspd_mode')" in html),
    "覆盖:高度随波动摆动已说明": "两者都会随「高度波动幅度」" in html,
    "覆盖:速度与轨迹不符有警告": "容易被判为伪造" in html,
    # 两个频段都放开自由选信道：5.8G 允许 149-165（20 MHz 网格），默认 149
    "信道:5.8G 可选149-165": "Wi-Fi 信道 (149/153/157/161/165)" in html,
    "信道:5.8G 不再禁用输入": "c.disabled=true" not in html,
    "信道:2.4G 仍可选1-13": "Wi-Fi 信道 (1-13)" in html,
    "信道:切频段会还原对应默认值": ("c.value=(LASTCFG&&LASTCFG.channel_5g)||149" in html
                                and "c.value=(LASTCFG&&LASTCFG.channel)||6" in html),
    "信道:保存时按频段提交5G信道": "d.channel_5g=_b===1?d.channel:(_L.channel_5g||149)" in html,
    # 说明信息一律折叠（点击展开），不允许平铺把卡片撑高
    "折叠:飞行参数说明": "<summary>说明：高度换算与航向/垂直速度来源</summary>" in html,
    "折叠:巡游说明": "<summary>说明：轨迹速度与报文速度</summary>" in html,
    "折叠:飞手说明": "<summary>说明：飞手坐标与无人机位置</summary>" in html,
    "折叠:无平铺长说明": ("class='note'>相对高度" not in html
                       and "class='note' id='patrolCalc'" not in html
                       and "class='note'>修改飞手坐标" not in html),
    # 两列自平衡：飞手位置挪到左列（身份信息卡之后、飞行参数列之前）
    "布局:飞手位置在左列": (html.find("id='operator_id'") < html.find("<h3>飞手位置</h3>")
                          < html.find("<h3>飞行参数</h3>")),
    # 顶部横幅：项目标题 + 仓库地址 + 右上角固件升级入口（含当前版本号）
    # 标题里放 <wbr>：手机窄屏只在「_Scan_Sim」前断行，不会把 ID 拆到中间
    "头部:项目标题": ("<h2><span class='dot'></span><span class='hdr-nm'>"
                  "ESP32-ChinaRemoteID<wbr>_Scan_Sim</span></h2>") in html,
    "头部:仓库地址": ("https://github.com/sd1471123/esp32-ChinaRemoteID_Scan_Sim" in html
                  and "target='_blank'" in html),
    # 地址整行独占 + 不折行：整条 url 在窄屏也要一行放下
    "头部:地址不折行": "white-space:nowrap" in html,
    "头部:升级入口在右上角": (html.find("id='otatoggle'") < html.find("<div class='wrap'>")
                          and 0 < html.find("id='otatoggle'")),
    "头部:入口含版本号": ("id='otavh'" in html
                     and html.find("id='otavh'") > html.find("id='otatoggle'")
                     and "hv.textContent='v'+(OTA.ver" in html),
    # S3（2.4G-only）：频段二选一整块 + 说明里的 2.4G/5.8G 那句一起收起
    "频段:无5G时整块隐藏": ("id='bandbox'" in html and "id='bandln'" in html
                       and "if(!j.has5g)" in html),
    # 固件升级（OTA）：顶部弹层 + 裸流上传 + 上传进度 + 重启后比对版本
    "OTA:面板存在": ("id='otamask'" in html and "<h4>固件升级 (OTA)</h4>" in html),
    "OTA:版本/分区/容量信息": all(("id='%s'" % i) in html
                            for i in ('otaver', 'otapart', 'otacan')),
    "OTA:文件选择与按钮": ("id='otafile'" in html and "onclick='otaStart()'" in html),
    "OTA:进度条": "id='otabar'" in html and "id='otafill'" in html,
    "OTA:裸流上传带令牌": ("'/api/ota/upload'" in html
                       and "'application/octet-stream'" in html
                       and "'X-OTA-Token'" in html),
    "OTA:用XHR拿上传进度": "x.upload.onprogress" in html,
    "OTA:升级时暂停轮询": "if(otaBusy)return;" in html,
    "OTA:重启后比对版本再刷新": ("function otaWaitReboot(" in html
                            and "location.reload()" in html),
    "OTA:升级说明折叠": "<summary>说明：升级流程、断电保护与风险</summary>" in html,
    # 历史轨迹（接收端持久化日志）：只有接收模式落盘，发射模式整卡隐藏
    "轨迹:卡片存在": ("id='logbox'" in html and "历史轨迹" in html
                  and "id='lghd'" in html),
    "轨迹:标题可折叠": ("onclick='logFold()'" in html and "id='lgbody'" in html
                    and "id='lgtog'" in html),
    "轨迹:详情可收起": ("id='lgdet'" in html and "onclick='logClose()'" in html
                    and "收起轨迹" in html),
    "轨迹:仅接收端显示": ("class='card span2 rxonly' id='logbox'" in html
                       and "lg.style.display=rx?'':'none'" in html
                       and "if(roleTx())return;" in html),
    "轨迹:统计三栏": all(("id='%s'" % i) in html for i in ('lgcnt', 'lgused', 'lgrange')),
    "轨迹:容量进度条": "class='lgbar'" in html and "id='lgfill'" in html,
    "轨迹:目标列表容器": "id='lgtargets'" in html and "id='lgpoints'" in html,
    "轨迹:导出与清空按钮": "onclick='logExport()'" in html and "onclick='logClear()'" in html,
    "轨迹:四个接口都在": all(("/api/log/%s" % i) in html
                        for i in ('info', 'points', 'export', 'clear')),
    "轨迹:轮询刷新": "setInterval(logRefresh,8000)" in html,
    "轨迹:未对时降级为上面计时": ("上电 '" in html and "fmtT(" in html),
    # 分页：30 条一页、最多 10 页，更早的只能导出 CSV
    "轨迹:每页30条最多10页": ("const LG_PER=30,LG_MAXP=10;" in html),
    "轨迹:分页带偏移": ("'&offset='+(lgPage*LG_PER)" in html),
    "轨迹:页码按钮": ("data-pg=" in html and "上一页" in html and "下一页" in html
                  and "更早的请导出 CSV" in html),
    # 航线回放：地图走独立模块 RXMAP.replay，页内只留 canvas 兜底
    "轨迹:回放控制": all(("id='%s'" % i) in html for i in ('lgpp', 'lgrs', 'lgnow', 'lgrate')),
    "轨迹:地图与canvas兜底": ("id='lgmap'" in html and "id='lgcv'" in html
                          and "RXMAP.replay(" in html and "function lgCanvas(){" in html),
    "轨迹:说明折叠": "<summary>说明：记录了什么、能存多久、时间为什么不对</summary>" in html,
    "轨迹:说明写出降采样与容量": ("最快 5 秒一条" in html and "6.5 万条" in html),
    # 热点密码（WPA2）：挡住陌生人连入热点改参数 / 上传固件
    "密码:输入框存在且限长63": "id='ap_password'" in html and "maxlength='63'" in html,
    "密码:紧跟在 SSID 之后": 0 < html.find("id='ssid'") < html.find("id='ap_password'")
                            < html.find("id='channel'"),
    "密码:提示留空等于开放": "留空 = 开放热点" in html,
    "密码:说明折叠": "<summary>说明：热点密码与设备安全</summary>" in html,
    "密码:说明写出改后要重连": ("所有已连接的手机都会被断开" in html
                          and "串口日志开机会打印当前 SSID 与口令" in html),
    "密码:保存后提示用新密码重连": "热点密码已修改，请用新密码重新连接" in html,
    "密码:随配置一起上报": "'ap_password'" in html and "d.ap_password" in html,
    "密码:OTA说明改为需连热点": "热点默认开启 WPA2 口令" in html,
}
for k, v in checks.items():
    print(("OK   " if v else "FAIL ") + k)
if r.returncode != 0 or depth != 0 or not all(checks.values()):
    raise SystemExit(1)
