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
    "高度:运行状态整行": ".g .full{grid-column:1/-1}" in html and "class='full'" in html,
}
for k, v in checks.items():
    print(("OK   " if v else "FAIL ") + k)
if r.returncode != 0 or depth != 0 or not all(checks.values()):
    raise SystemExit(1)
