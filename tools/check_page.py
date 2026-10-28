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
    "页面仅通过 RXMAP 钩子调用": len([l for l in html.splitlines() if "RXMAP" in l]) == 5,
    "离线网格容器在": "id='rxgrid'" in html and "id='rxgridnote'" in html,
    "标题已更新": "<title>ESP32-RID 接收/发射</title>" in html
                and "ESP32-RID 接收/发射</h2>" in html
                and "开源项目根据 GB 42590-2023 / GB 46750-2025" in html,
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
}
for k, v in checks.items():
    print(("OK   " if v else "FAIL ") + k)
if r.returncode != 0 or depth != 0 or not all(checks.values()):
    raise SystemExit(1)
