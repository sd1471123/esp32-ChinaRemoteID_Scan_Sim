"""机型预设表生成 / 校验（数据源：FAA UAS Declaration of Compliance 快照）。

页面里的「机型预设（伪装）」只允许出现能被官方数据核实的条目：
    1) 前缀是 FAA 登记的 serialNumberStart，或它的合法截短（>=7 位）；
    2) 型号名与 FAA 的 model 字段对应（允许缩写，映射写在 CURATED 里）。

用法：
    python tools/models_faa.py --check   # 校验 crid_web.c 里的表是否与数据库同步
    python tools/models_faa.py --write    # 用数据库改写 crid_web.c 里的表
    python tools/models_faa.py --doc      # 生成 docs/model_db.md

不再允许「厂商码 + 000/001 递增」这类推断前缀进入固件。
"""
import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAA = os.path.join(ROOT, "data", "faa_doc_2026-11-11.txt")
DRAFT = os.path.join(ROOT, "data", "models_draft_2026-11-11.txt")
SRC = os.path.join(ROOT, "main", "crid_web.c")
DOC = os.path.join(ROOT, "docs", "model_db.md")
SNAPSHOT_DATE = "2026-11-11"

# ---------------------------------------------------------------- 数据解析
def parse_faa(path):
    """返回 [(vendor, model, [prefix...])]，保持文件顺序。"""
    rows, vendor, started = [], None, False
    for line in io.open(path, encoding="utf-8").read().splitlines():
        m = re.match(r"^={10} (.+?) ={10}$", line)
        if m:
            vendor, started = m.group(1).strip(), True
            continue
        if not started:
            continue
        if re.match(r"^  \S", line):
            rows.append((vendor, line.strip(), []))
        else:
            m = re.match(r"^    匹配前缀: (.+)$", line)
            if m and rows:
                rows[-1][2].extend(x.strip() for x in m.group(1).split(","))
    return rows


# ---------------------------------------------------------------- 已核实条目
# (分组, 前缀, 页面显示名, FAA 官方型号, 备注)
CURATED = [
    # ---- 消费级 ----
    ("消费级", "1581F0M6", "Mavic 2 Zoom", "Mavic 2 Zoom", ""),
    ("消费级", "1581F11V", "Phantom 4 Pro V2.0", "Phantom 4 Pro V2.0", ""),
    ("消费级", "1581F163", "Mavic 2 Pro", "Mavic 2 Pro", ""),
    ("消费级", "1581F1WN", "Mavic Air 2", "Mavic Air 2 (MA2UE1N)", "官方区分 MA2UE1N/MA2UE3W"),
    ("消费级", "1581F3N3", "Mavic Air 2", "Mavic Air 2 (MA2UE3W)", ""),
    ("消费级", "1581F3YT", "Air 2S", "Air 2S", ""),
    ("消费级", "1581F45T", "Mavic 3", "Mavic 3", ""),
    ("消费级", "1581F4QZ", "Mavic 3 Cine", "Mavic 3 Cine", ""),
    ("消费级", "1581F4XF", "Mini 3 Pro", "Mini 3 Pro", ""),
    ("消费级", "1581F5YH", "Mini 3", "Mini 3", ""),
    ("消费级", "1581F67P", "Mavic 3 Classic", "Mavic 3 Classic", ""),
    ("消费级", "1581F67Q", "Mavic 3 Pro", "Mavic 3 Pro", ""),
    ("消费级", "1581F6L5", "Mini 3 Pro", "Mini 3 Pro", ""),
    ("消费级", "1581F6MK", "Mavic 3 Pro Cine", "Mavic 3 Pro Cine", ""),
    ("消费级", "1581F6N8", "Air 3", "Air 3", ""),
    ("消费级", "1581F6Z9", "Mini 4 Pro", "Mini 4 Pro", ""),
    ("消费级", "1581F895", "Air 3S", "Air 3S", ""),
    ("消费级", "1581F8LQ", "Mavic 4 Pro (64GB)", "Mavic 4 Pro (64 GB)", ""),
    ("消费级", "1581F986", "Mavic 4 Pro (512GB)", "Mavic 4 Pro (512 GB)", ""),
    ("消费级", "1581F9DE", "Mini 5 Pro", "Mini 5 Pro", ""),
    # ---- 穿越机 ----
    ("穿越机", "1581F37Q", "FPV", "FPV", ""),
    ("穿越机", "1581F4QW", "Avata", "AVATA", "官方写作 AVATA"),
    ("穿越机", "1581F6W8", "Avata 2", "Avata 2", ""),
    ("穿越机", "1581FA8J", "Avata 360", "Avata 360 (DVN3XT)", ""),
    ("穿越机", "1581FBLK", "Avata 360", "DJI Avata 360 (DVN3NT)", "两个硬件版本共用显示名"),
    # ---- 行业级 ----
    ("行业级", "1581F0UY", "Phantom 4 RTK", "Phantom 4 RTK", ""),
    ("行业级", "1581F1ZN", "Matrice 300 RTK", "M300 RTK", ""),
    ("行业级", "1581F4BN", "Matrice 30", "M30", ""),
    ("行业级", "1581F4GC", "Mavic 2 Enterprise Advanced", "Mavic 2 Enterprise Advanced", ""),
    ("行业级", "1581F4Z4", "Inspire 3", "Inspire 3", ""),
    ("行业级", "1581F5BK", "Matrice 30T", "M30T", ""),
    ("行业级", "1581F5BL", "Matrice 30 (机场版)", "M30 Dock version", ""),
    ("行业级", "1581F5BM", "Matrice 30T (机场版)", "M30T Dock version", ""),
    ("行业级", "1581F5FH", "Mavic 3E", "M3E", ""),
    ("行业级", "1581F5FJ", "Mavic 3T", "M3T", ""),
    ("行业级", "1581F5FK", "Mavic 3M", "M3M", ""),
    ("行业级", "1581F62H", "Matrice 30T", "M30T (EU&KR)", "欧韩版，与 M30T 同名"),
    ("行业级", "1581F6GK", "Matrice 350 RTK", "M350 RTK", ""),
    ("行业级", "1581F6Q8", "Matrice 3TD", "M3TD", ""),
    ("行业级", "1581F6QA", "Matrice 3D", "M3D", ""),
    ("行业级", "1581F7FV", "Matrice 4E", "Matrice 4E", ""),
    ("行业级", "1581F7K3", "Matrice 4T", "Matrice 4T", ""),
    ("行业级", "1581F8DB", "Matrice 400", "Matrice 400", ""),
    ("行业级", "1581F8HG", "Matrice 4TD", "Matrice 4TD", ""),
    ("行业级", "1581F8HH", "Matrice 4D", "Matrice 4D", ""),
    ("行业级", "1581FBEX", "Matrice 3TA", "M3TA", ""),
    # ---- 植保 ----
    ("植保", "1581F446", "Agras T10", "Agras T10", "T10/T30 共用前缀"),
    ("植保", "1581F446", "Agras T30", "Agras T30", ""),
    ("植保", "1581F574", "Agras T20P", "Agras T20P", "T20P/T40 共用前缀"),
    ("植保", "1581F574", "Agras T40", "Agras T40", ""),
    ("植保", "1581F6BU", "Agras T50", "Agras T50 & T25", "官方含 T25 别名"),
    ("植保", "1581F836", "Agras T25P", "Agras T25P", ""),
    ("植保", "1581F8ZL", "Agras T100", "Agras T100", ""),
    ("植保", "1581F8ZX", "Agras T70P", "Agras T70P", ""),
    # ---- 物流 ----
    ("物流", "1581F7C6", "FlyCart 30", "FlyCart 30", ""),
    ("物流", "1581FAN4", "FlyCart 100", "FlyCart 100", ""),
    # ---- 其他厂商（前缀为官方区间起点的合法截短）----
    ("其他厂商", "1748FEV4", "Autel Alpha", "Autel Alpha", "官方 1748FEV4ALP9"),
    ("其他厂商", "1748CJD2", "Dragonfish Lite", "Dragonfish Lite", "官方 1748CJD2822390001"),
    ("其他厂商", "1748FEV3", "EVO Max", "EVO Max", "与 MDX-1 共用前缀"),
    ("其他厂商", "1748FEV3", "MDX-1", "MDX-1", ""),
    ("其他厂商", "1748CHL7", "EVO II", "EVO II", "与 EVO II V3 前缀重叠"),
    ("其他厂商", "1748CLT0", "EVO Lite", "EVO Lite", "官方 1748CLT09222856"),
    ("其他厂商", "1748CLTG", "EVO Lite", "EVO Lite", "官方 1748CLTG921491"),
    ("其他厂商", "1748CLTC", "EVO Lite+", "EVO Lite+", "官方同前缀也登记 EVO Lite"),
    ("其他厂商", "1748CNA0", "EVO Nano", "EVO Nano", "官方 1748CNA0922023"),
    ("其他厂商", "1588E040", "Anafi AI", "ANAFI Ai", "Anafi 家族公共前缀（USA 为 …505）"),
]

GROUP_ORDER = ["消费级", "穿越机", "行业级", "植保", "物流", "其他厂商"]


def validate(rows):
    """逐条核对 CURATED，返回错误列表。"""
    errs = []
    index = {}
    for v, m, ps in rows:
        index.setdefault(m, []).append((v, ps))
    for grp, px, disp, official, note in CURATED:
        cands = index.get(official)
        if not cands:
            errs.append("官方型号不存在: %s (%s)" % (official, disp))
            continue
        ok = False
        for v, ps in cands:
            for p in ps:
                if p == px:
                    ok = True
                elif p.startswith(px) and len(px) >= 7:
                    ok = True
                elif px.startswith(p) and len(p) >= 6:
                    ok = True
        if not ok:
            errs.append("前缀无官方依据: %s %s -> %s" % (px, disp, official))
    return errs


# ---------------------------------------------------------------- 渲染
def render_tokens():
    toks = ["const MDL=["]
    for grp in GROUP_ORDER:
        items = [c for c in CURATED if c[0] == grp]
        if not items:
            continue
        toks.append("['%s',[" % grp)
        for i, (_, px, disp, _, _) in enumerate(items):
            toks.append("['%s','%s']" % (px, disp))
            if i != len(items) - 1:
                toks.append(",")
        toks.append("]],")
    toks[-1] = "]]"  # 去掉最后一个组后的逗号
    toks.append("];")  # 关闭 MDL 数组
    return toks


def render_lines(width=98):
    lines, cur = [], ""
    for t in render_tokens():
        if cur and len(cur) + len(t) > width and t not in (",", "]],"):
            lines.append(cur)
            cur = ""
        cur += t
    if cur:
        lines.append(cur)
    return lines


def render_block():
    return "\n".join('"%s"' % l for l in render_lines())


def find_block(src):
    for marker in ('"const MDL=[', '"const DJI=['):
        if marker in src:
            start = src.index(marker)
            break
    else:
        raise SystemExit("未找到机型表块")
    tail = ']]];"'
    if tail in src[start:]:
        end = src.index(tail, start) + len(tail)
    else:
        end = src.index('];"', start) + len('];"')
    return start, end


# ---------------------------------------------------------------- 主流程
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--doc", action="store_true")
    args = ap.parse_args()

    rows = parse_faa(FAA)
    errs = validate(rows)
    if errs:
        print("校验失败：")
        for e in errs:
            print("  " + e)
        return 1

    src = io.open(SRC, encoding="utf-8").read()
    block = render_block()
    start, end = find_block(src)
    current = src[start:end]

    if args.write:
        io.open(SRC, "w", encoding="utf-8", newline="\n").write(src[:start] + block + src[end:])
        print("已写入 %s（%d 条 / %d 个前缀）" % (SRC, len(CURATED), len({c[1] for c in CURATED})))
    elif args.check:
        if current == block:
            print("机型表与 FAA 快照一致（%d 条）" % len(CURATED))
        else:
            print("机型表已漂移，请运行 --write")
            return 1
    if args.doc:
        write_doc(rows)
        print("已生成 %s" % DOC)
    return 0


# ---------------------------------------------------------------- 文档
# 机型库草稿中「官方有该型号、但前缀不成立」的条目
CORRECTIONS = [
    ("消费级", "11223344", "Anafi", "ANAFI Ai", "1588E040452AA000001", "8 位占位数字，非厂商码"),
    ("消费级", "44332211", "Anafi USA", "ANAFI USA", "1588E040505AA000001", "同上，且与 Anafi AI 混用"),
    ("消费级", "1581FANL", "Mini 5 Pro (4G)", "Mini 5 Pro", "1581F9DE", "官方无 4G 独立登记"),
    ("消费级", "1748FEV2", "EVO II V3", "EVO II V3", "1748CHL7823", "厂商码后两位写错"),
    ("消费级", "P208904", "Skydio 2 / 2+", "2 SDRC2V1 / 2+ SDR35V1", "1668F / 1668BS20FB", "把型号代号当序列号前缀"),
    ("行业级", "1701A000", "Percepto Max", "Percepto AirMax", "1701DSPR / 1701ASPR", "型号名与前缀均不符"),
    ("行业级", "1748CJD1", "Dragonfish Standard", "Dragonfish Standard", "无区间", "官方只登记型号，未给序列号"),
    ("行业级", "1748CJD3", "Dragonfish Pro", "Dragonfish Pro", "1748FDF3JD3924", "前缀应为 FDF3，非 CJD3"),
    ("行业级", "SDR21V1", "Skydio X2E", "X2E SDR21V1", "1668BE10JA", "把型号代号当序列号前缀"),
    ("行业级", "SDR35V1", "Skydio X2", "2+ SDR35V1", "1668BS20FB", "SDR35V1 实为 Skydio 2+，型号也错"),
    ("行业级", "SR47PCV", "Skydio X10", "X10E SR47P", "1668BR40EA", "X10 无直接登记，仅有 X10E/R10"),
    ("行业级", "1758X000", "P150 Max", "P150 Max", "1863F251", "厂商码应为 1863F，非 1758X"),
    ("行业级", "1758X001", "P100 Pro", "P100 Pro", "1863FPEG8219", "同上"),
    ("行业级", "1758X002", "P100", "P100", "1863FPEG8218", "同上"),
    ("电影级", "18179000", "Alta X", "Alta X", "无区间", "官方只登记型号，未给序列号"),
    ("电影级", "18179001", "Alta X Gen2", "Alta X Gen2", "无区间", "同上"),
    ("测绘级", "1567A000", "md4-1000", "md4-1000", "1644FMD4100000007004", "厂商码应为 1644FMD4"),
    ("测绘级", "1567A001", "md4-3000", "md4-3000", "1644FMD4300000001234", "同上"),
    ("测绘级", "1587A000", "eBee X", "eBee X series", "1587A051…/1587A057…", "厂商码应为 1587A05/1587A54"),
    ("测绘级", "1587A001", "eBee TAC", "eBee X series", "1587A051…", "同上"),
    ("测绘级", "1587A002", "eBee Geo", "eBee X series", "1587A051…", "同上"),
    ("测绘级", "1688W000", "WingtraOne Gen II", "WingtraOne Gen II", "无区间", "官方只登记型号，未给序列号"),
]

# 官方清单中完全没有该型号
UNREGISTERED = [
    ("消费级", "1581F6CD", "Mini 2 SE", "DJI 未在 DoC 登记该型号"),
    ("消费级", "1581F8PJ", "Mini 4K", "DJI 未在 DoC 登记该型号（固件另一处写作 1581F8C8）"),
    ("消费级", "1581FB34", "Lito X1", "无任何厂商登记"),
    ("消费级", "1581FBV5", "Lito 1", "无任何厂商登记"),
    ("消费级", "1778S000", "SplashDrone 4", "SwellPro 只登记 A-RID/FD2/FD3"),
    ("消费级", "PF723000", "Rolling Spider", "Parrot 未登记"),
    ("消费级", "PF727078", "Mambo FLY", "Parrot 未登记"),
    ("消费级", "YUNQ4KPUS", "Q500 4K Typhoon", "Yuneec 全品牌未登记"),
    ("穿越机", "1581F7V2", "Flip", "DJI 未在 DoC 登记该型号"),
    ("穿越机", "1581F87L", "Neo", "DJI 未在 DoC 登记该型号"),
    ("穿越机", "1581FA6Q", "Neo 2", "DJI 未在 DoC 登记该型号"),
    ("行业级", "1701A001", "Percepto Sparrow", "官方只有 AirMax/OGI"),
    ("行业级", "1701A002", "Percepto Air", "官方只有 AirMax/OGI"),
    ("行业级", "YUNH5ETWET", "H520 Hexacopter", "Yuneec 未登记"),
    ("行业级", "YUNH920202EU", "Tornado H920", "Yuneec 未登记"),
    ("行业级", "YUNTYH3EU", "Typhoon H3", "Yuneec 未登记"),
    ("植保", "1581FAE3", "Agras T70S", "DJI 只登记 T70P"),
    ("植保", "1758X003", "V40", "XAG 无该型号（有 P60）"),
    ("电影级", "18179002", "Alta 8", "Freefly 未登记"),
    ("测绘级", "1587A003", "eBee RTK", "官方 eBee X series 不含 RTK"),
    ("测绘级", "1688W001", "WingtraOne GEN I", "Wingtra 只登记 Gen II / RAY"),
]


def _official_for(rows, official, prefix=""):
    """取该型号下与给定前缀相关的官方号段（最多两条，避免拼串被截断）。"""
    out = []
    for v, m, ps in rows:
        if m == official:
            out.extend(ps)
    if prefix:
        out = [p for p in out if p.startswith(prefix) or prefix.startswith(p)]
    out = list(dict.fromkeys(out))
    out.sort(key=len, reverse=True)
    return out[:2]


def write_doc(rows):
    os.makedirs(os.path.dirname(DOC), exist_ok=True)
    n_all = len(CURATED)
    n_px = len({c[1] for c in CURATED})
    L = []
    A = L.append
    A("# 机型库可信清单（FAA UAS DoC）")
    A("")
    A("> 数据源：FAA UAS Declaration of Compliance（<https://uasdoc.faa.gov>），快照 `data/faa_doc_%s.txt`（516 条声明 / 170 家厂商）。" % SNAPSHOT_DATE)
    A("> 本文档由 `python tools/models_faa.py --doc` 从同一份快照生成，与固件 `main/crid_web.c` 中的机型预设表共用一份数据，不要手工改表。")
    A("")
    A("## 一、判定规则")
    A("")
    A("一条机型记录只有在**同时满足**下面两点时才算「已核实」：")
    A("")
    A("1. **前缀成立**：前缀等于官方 `serialNumberStart`，或是它的合法截短（≥7 位，仍落在官方号段内）；")
    A("2. **型号对得上**：型号名能与 FAA `model` 字段一一对应（允许 `M3E`↔`Mavic 3E` 这类官方缩写）。")
    A("")
    A("不满足的，一律不进固件：官方没给号段的（Alta X、Wingtra、Dragonfish Standard）不猜；")
    A("把型号代号当序列号的（`SDR21V1`、`SR47PCV`）不算；「厂商码 + 000/001 递增」这类推断前缀不算。")
    A("")
    A("## 二、结果")
    A("")
    A("| 判定 | 条数 |")
    A("| --- | --- |")
    A("| ✅ 已核实，已进入固件 | **%d**（%d 个前缀） |" % (n_all, n_px))
    A("| ⚠️ 官方有型号、但前缀不成立（需修正，未进固件） | %d |" % len(CORRECTIONS))
    A("| ❌ 官方清单无该型号（未进固件） | %d |" % len(UNREGISTERED))
    A("")
    A("## 三、已核实清单（固件当前使用的全部条目）")
    A("")
    for grp in GROUP_ORDER:
        items = [c for c in CURATED if c[0] == grp]
        if not items:
            continue
        A("### %s（%d 条）" % (grp, len(items)))
        A("")
        A("| 前缀 | 显示名 | FAA 官方型号 | 官方号段 / 前缀 | 备注 |")
        A("| --- | --- | --- | --- | --- |")
        for _, px, disp, official, note in items:
            off = _official_for(rows, official, px)
            shown = "、".join(off) if off else "—"
            A("| `%s` | %s | %s | `%s` | %s |" % (px, disp, official, shown, note or ""))
        A("")
    A("## 四、需要修正的条目（官方有型号，前缀不成立）")
    A("")
    A("| 分类 | 草稿前缀 | 草稿型号 | FAA 官方型号 | 官方号段 | 问题 |")
    A("| --- | --- | --- | --- | --- | --- |")
    for sec, px, nm, off, val, why in CORRECTIONS:
        A("| %s | `%s` | %s | %s | `%s` | %s |" % (sec, px, nm, off, val, why))
    A("")
    A("## 五、官方清单中不存在的型号（UNVERIFIED）")
    A("")
    A("这些条目只保留在文档里备查，**不进固件、不参与任何匹配**。缺失原因多为该机型未在美国登记 DoC，")
    A("不代表机器不存在，但在拿到官方依据前不应给它们编前缀。")
    A("")
    A("| 分类 | 草稿前缀 | 型号 | 说明 |")
    A("| --- | --- | --- | --- |")
    for sec, px, nm, why in UNREGISTERED:
        A("| %s | `%s` | %s | %s |" % (sec, px, nm, why))
    A("")
    A("## 六、共用一个前缀的情况")
    A("")
    A("官方数据里确实存在「一个号段对应两个型号」，固件保留多条同名记录，选型时随机取前缀：")
    A("")
    A("| 前缀 | 涉及型号 | 处理 |")
    A("| --- | --- | --- |")
    A("| `1581F446` | Agras T10 / T30 | 两条记录共用，型号名由使用者在预设里选 |")
    A("| `1581F574` | Agras T20P / T40 | 同上 |")
    A("| `1748FEV3` | Autel EVO Max / MDX-1 | 同上 |")
    A("| `1748CLTC` | Autel EVO Lite / EVO Lite+ | 官方同号段，显示名取 EVO Lite+ |")
    A("| `1748CHL7` | Autel EVO II / EVO II V3 | 官方号段重叠，取 EVO II |")
    A("| `1588E040` | Parrot Anafi AI / USA / UKR GOV | 家族公共前缀，取 Anafi AI |")
    A("")
    A("## 七、为什么只认 FAA")
    A("")
    A("- FAA DoC 是目前唯一**公开、可逐条复核**「型号 ↔ 序列号号段」的官方登记，其余来源都拿不出这个对应关系；")
    A("- 厂商官网与说明书只写型号，不公布 RID 序列号前缀；社区整理的机型库没有出处，出错也无法追溯——旧表里的")
    A("  `18179000`、`1587A000`、`1758X000` 这类「厂商码 + 递增序号」就是无从核对的产物；")
    A("- FAA 只覆盖在美国登记的机型，**未登记不等于不存在**：这类条目单列 UNVERIFIED，等拿到官方依据再入库，")
    A("  期间固件不为它们生成序列号，避免产出查无此机的号码。")
    A("")
    A("## 八、维护")
    A("")
    A("```bash")
    A("python tools/models_faa.py --check   # CI/本地回归：表是否与快照一致")
    A("python tools/models_faa.py --write   # 改了 CURATED 后同步进 crid_web.c")
    A("python tools/models_faa.py --doc     # 重新生成本文档")
    A("```")
    A("")
    A("换新快照时：把新的 FAA 导出放到 `data/faa_doc_YYYY-MM-DD.txt`，改脚本里的 `FAA`/`SNAPSHOT_DATE`，")
    A("先跑 `--check` 看哪些条目失效，再决定是保留旧前缀还是随官方更新。")
    A("")
    A("快照取自 `uasdoc.faa.gov` 的公开接口（`/api/v1/publicDOCRev`，无需登录），按公司分组整理成")
    A("「厂商 → 型号 → 匹配前缀」三段式，即 `data/faa_doc_2026-11-11.txt` 的格式。")
    A("")
    io.open(DOC, "w", encoding="utf-8", newline="\n").write("\n".join(L))


if __name__ == "__main__":
    sys.exit(main())
