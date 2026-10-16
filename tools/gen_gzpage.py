"""生成内置网页的 gzip 压缩版头文件（勿手改，由 build.ps1 每次编译前调用）。"""
import gzip, hashlib, io, os

# 仓库根目录 = 本脚本所在目录的上一级（本地与 CI 均可用，不写死绝对路径）
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(BASE, "main", "crid_web.c")
RXMAP_SRC = os.path.join(BASE, "main", "rxmap_page.js")
OUT = os.path.join(BASE, "main", "page_gz.h")

src = io.open(SRC, encoding="utf-8").read()


def extract_array(name):
    """按 C 语法扫描，取出 static const char NAME[] = "..." ...; 的全部字符串字面量并反转义。

    逐字符扫描并跟踪「是否在字符串内」，遇到字符串外的 ';' 即数组结束，
    这样不会把数组后面的 C 代码（如注释里的引号）误收进来。
    """
    decl = "static const char %s[] =" % name
    i = src.index(decl)
    k = i + len(decl)
    lits = []
    cur = None
    esc = False
    while k < len(src):
        c = src[k]
        if cur is None:
            if c == '"':
                cur = []
                esc = False
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
        raise SystemExit("%s 数组未正常闭合（源码可能被改坏）" % name)
    return "".join(lits)


html = extract_array("PAGE_HTML")
assert "</html>" in html, "PAGE_HTML 提取不完整"
assert "async function pollRx" in html, "PAGE_HTML 提取不完整（缺少 pollRx）"

gz_html = gzip.compress(html.encode("utf-8"), 9)
print(f"html {len(html)} bytes -> gz {len(gz_html)} bytes")

# ---- 接收端地图脚本：独立资源 /rxmap.js ----
# 源码是独立 JS 文件（main/rxmap_page.js），可单独 lint；这里编成固件资源。
# 同时产出明文与 gzip 两份，供 handler_rxmap 按浏览器能力选择。
rx_src = io.open(RXMAP_SRC, encoding="utf-8").read()
gz_rx = gzip.compress(rx_src.encode("utf-8"), 9)
print(f"rxmap {len(rx_src)} bytes -> gz {len(gz_rx)} bytes")


def c_string_literal(text, per_line=110):
    """把任意文本转成 C 字符串字面量行（逐行拆分，保持头文件可读）。"""
    lines = []
    buf = ""
    for ch in text:
        if ch == "\\":
            buf += "\\\\"
        elif ch == '"':
            buf += '\\"'
        elif ch == "\n":
            buf += "\\n"
            lines.append(buf)
            buf = ""
        else:
            buf += ch
        if len(buf) >= per_line:
            lines.append(buf)
            buf = ""
    if buf:
        lines.append(buf)
    if not lines:
        lines = [""]
    return lines


hdr = ("// 自动生成：tools/gen_gzpage.py（勿手改）—— 内置网页与接收端地图脚本\n"
       "// 由 build.ps1 在每次编译前重新生成\n"
       "#pragma once\n#include <stdint.h>\n\n"
       f"// gzip sha256: html={hashlib.sha256(gz_html).hexdigest()} "
       f"rxmap={hashlib.sha256(gz_rx).hexdigest()}\n"
       f"const uint8_t PAGE_HTML_GZ[] = {{{','.join(str(b) for b in gz_html)}}};\n"
       f"const unsigned int PAGE_HTML_GZ_LEN = {len(gz_html)};\n\n"
       f"const uint8_t RXMAP_JS_GZ[] = {{{','.join(str(b) for b in gz_rx)}}};\n"
       f"const unsigned int RXMAP_JS_GZ_LEN = {len(gz_rx)};\n\n"
       "// 接收端地图脚本明文（浏览器不支持 gzip 时回退）\n"
       "const char RXMAP_JS[] =\n")
for i, ln in enumerate(c_string_literal(rx_src)):
    hdr += '    "%s"%s\n' % (ln, "" if i == len(c_string_literal(rx_src)) - 1 else "")
hdr += ";\n"

if os.path.exists(OUT):
    old = io.open(OUT, encoding="utf-8").read()
    if "gzip sha256: " in old:
        old_line = old.split("gzip sha256: ")[1].split("\n")[0].strip()
        new_line = hdr.split("gzip sha256: ")[1].split("\n")[0].strip()
        if old_line == new_line:
            print("page_gz.h unchanged, skip")
            raise SystemExit(0)

io.open(OUT, "w", encoding="utf-8", newline="\n").write(hdr)
print("page_gz.h written")
