"""从编译产物 bin 中核对内置网页资源是否与源码一致。

做两件事：
1. 明文 PAGE_HTML 在 bin 中的完整性（HTML 里不应再出现应用代码）
2. page_gz.h 里的两份 gzip 数组是否原样进入 bin、解压后内容是否符合预期
"""
import gzip, io, os, re, sys

# 允许用第一个参数指定 bin，方便核对非默认构建目录（如 -B build_c5 的产物）
BIN = sys.argv[1] if len(sys.argv) > 1 else r"E:/esp32-crid-1.1.0/build/remoteid_scanner.bin"
HDR = r"E:/esp32-crid-1.1.0/main/page_gz.h"

data = open(BIN, "rb").read()
hdr = io.open(HDR, encoding="utf-8").read()


def arr(name):
    m = re.search(r"const uint8_t %s\[\] = \{([^}]*)\};" % name, hdr)
    vals = [int(x) for x in m.group(1).split(",") if x.strip()]
    return bytes(vals)


def arr_len(name):
    return int(re.search(r"const unsigned int %s = (\d+);" % name, hdr).group(1))


ok = True

# ---- 1. 明文 HTML ----
i = data.find(b"<!DOCTYPE html>")
if i < 0:
    print("FAIL 明文 HTML 未在 bin 中找到"); ok = False
else:
    j = data.find(b"</html>", i) + len(b"</html>")
    page = data[i:j].decode("utf-8", errors="replace")
    print("明文 HTML 长度:", len(page))
    for name, cond in {
        "应用脚本内联在页面中": "async function pollRx" in page,
        "无 /app.js 拆分": "/app.js" not in page,
        "无自检诊断代码": "selfCheck" not in page and "__probe" not in page,
        # 接收端地图架构：容器与 /rxmap.js 引用在页面里，建图逻辑必须仍在独立资源中
        "地图容器与独立资源引用在": "rxmapbox" in page and "/rxmap.js" in page,
        "接收端地图逻辑不内联": "L.map('rxmap')" not in page and "initRxMap" not in page,
        # 运行模式：接收/发射二选一 radio（不再是两个独立 checkbox）
        "角色拨动开关在": "name='role'" in page and "name='tx_en'" not in page,
        # 说明信息默认折叠（details 无 open 属性）
        "说明默认折叠": page.count("details class='tip'") >= 4,
        # 接收卡片导航按钮（跳手机地图 App）+ 点击后弹出的高德/百度选择层，
        # 且页面不再有常驻的地图下拉（id='navmap'）
        "导航按钮在": "class=\\\"navbtn\\\"" in page or 'class="navbtn"' in page,
        "导航选择层在": "id='navpick'" in page and "data-prov='amap'" in page
                        and "amapuri://route/plan" in page and "bdapp://map/direction" in page,
        "无常驻地图下拉": "id='navmap'" not in page and "class='navsel'" not in page,
        "网页兜底在": "uri.amap.com" in page and "api.map.baidu.com" in page,
        "iOS scheme 在": "iosamap://path" in page and "baidumap://map/direction" in page,
    }.items():
        print(("OK   " if cond else "FAIL ") + name)
        ok = ok and cond

# ---- 2. gzip 数组 ----
blob = arr("PAGE_HTML_GZ")
declared = arr_len("PAGE_HTML_GZ_LEN")
# gzip 头 10 字节里含 mtime：每次 gen_gzpage 重新生成时时间戳不同，头部字节就会变。
# 之前直接比对 blob[:64] 会在「先烧 S3、再烧 C5（C5 编译时又重生成 page_gz.h）」
# 之后永久报「在bin中=否」——是假警报。跳过 10 字节头，比对压缩数据+尾部 CRC。
in_bin = data.find(blob[10:]) >= 0
try:
    plain = gzip.decompress(blob).decode("utf-8")
except Exception as e:
    plain = ""
    print("FAIL PAGE_HTML_GZ 解压失败:", e); ok = False
markers = ["</html>", "async function pollRx", "unpkg.com/leaflet@1.9.4/dist/leaflet.js", "/rxmap.js"]
cond_len = len(blob) == declared
cond_marks = all(mk in plain for mk in markers)
print("PAGE_HTML_GZ: %d 字节(声明 %d) 在bin中=%s 解压后 %d 字节 标记=%s"
      % (len(blob), declared, "是" if in_bin else "否", len(plain), "全" if cond_marks else "缺"))
ok = ok and cond_len and cond_marks and in_bin

print("\n" + ("ALL BIN CHECKS PASS" if ok else "BIN CHECKS FAILED"))
raise SystemExit(0 if ok else 1)
