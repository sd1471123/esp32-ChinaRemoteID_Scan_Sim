"""从编译产物 bin 中核对内置网页资源是否与源码一致。

做两件事：
1. 明文 PAGE_HTML 在 bin 中的完整性（HTML 里不应再出现应用代码）
2. page_gz.h 里的两份 gzip 数组是否原样进入 bin、解压后内容是否符合预期
"""
import gzip, io, re

BIN = r"E:/esp32-crid-1.1.0/build/remoteid_scanner.bin"
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
        "无接收端地图": "rxmapbox" not in page and "initRxMap" not in page,
    }.items():
        print(("OK   " if cond else "FAIL ") + name)
        ok = ok and cond

# ---- 2. gzip 数组 ----
blob = arr("PAGE_HTML_GZ")
declared = arr_len("PAGE_HTML_GZ_LEN")
in_bin = data.find(blob[:64]) >= 0 and data.find(blob[-64:]) >= 0
try:
    plain = gzip.decompress(blob).decode("utf-8")
except Exception as e:
    plain = ""
    print("FAIL PAGE_HTML_GZ 解压失败:", e); ok = False
markers = ["</html>", "async function pollRx", "unpkg.com/leaflet@1.9.4/dist/leaflet.js"]
cond_len = len(blob) == declared
cond_marks = all(mk in plain for mk in markers)
print("PAGE_HTML_GZ: %d 字节(声明 %d) 在bin中=%s 解压后 %d 字节 标记=%s"
      % (len(blob), declared, "是" if in_bin else "否", len(plain), "全" if cond_marks else "缺"))
ok = ok and cond_len and cond_marks and in_bin

print("\n" + ("ALL BIN CHECKS PASS" if ok else "BIN CHECKS FAILED"))
raise SystemExit(0 if ok else 1)
