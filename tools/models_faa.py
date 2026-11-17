"""机型预设表生成 / 校验（数据源：FAA UAS DoC 官方序列号登记）。

权威数据来自 uasdoc.faa.gov 的两个公开端点：
    /api/v1/publicDOCRev/{trackingNumber}                 -> publicRevision.docRevId
    /api/v1/serialNumbers?findBy=docRevId&docRevId={id}   -> 该声明登记的全部序列号
快照见 data/faa_serial_2026-11-17.tsv（516 条声明 / 5004 条序列号条目）。

机型前缀由官方序列号推导，不再手工抄写：
    RANGE     取起点与终点的公共前缀（如 1581F45T000000000000-1581F45TZZZZZZZZZZZZ -> 1581F45T）
    列表      全部值取公共前缀；不足 5 位时取前 8 位众数（占比 >= 60%）
    单条      取前 8 位，标记低置信
    RANGE 起点前 8 位也作为合法截短接受（官方号段内更细的型号段）

用法：
    python tools/models_faa.py --check     # 校验 crid_web.c 里的表是否与官方数据一致
    python tools/models_faa.py --write     # 用 CURATED 改写 crid_web.c 里的表
    python tools/models_faa.py --doc       # 生成 docs/model_db.md
    python tools/models_faa.py --suggest   # 打印各厂商的官方前缀候选
"""
import argparse
import collections
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERIAL = os.path.join(ROOT, "data", "faa_serial_2026-11-17.tsv")
DRAFT = os.path.join(ROOT, "data", "models_draft_2026-11-11.txt")
SRC = os.path.join(ROOT, "main", "crid_web.c")
DOC = os.path.join(ROOT, "docs", "model_db.md")
INDEX = os.path.join(ROOT, "data", "faa_prefix_index_%s.tsv" % "2026-11-17")
REVIEW = os.path.join(ROOT, "data", "models_reviewed_%s.tsv" % "2026-11-17")
SNAPSHOT_DATE = "2026-11-17"
MIN_PREFIX = 5


# ---------------------------------------------------------------- 数据解析
def parse_serial(path=SERIAL):
    """返回 {trackingNumber: {rev, make, model, items:[(type, value)]}}，保持声明顺序。"""
    recs = {}
    for line in io.open(path, encoding="utf-8").read().splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 6:
            continue
        tn, rev, make, model, typ, val = f[0], f[1], f[2], f[3], f[4], f[5]
        r = recs.setdefault(tn, {"rev": rev, "make": make, "model": model, "items": []})
        if val:
            r["items"].append((typ, val))
    return recs


def common_prefix(vals):
    if not vals:
        return ""
    p = vals[0]
    for v in vals[1:]:
        i = 0
        while i < len(p) and i < len(v) and p[i] == v[i]:
            i += 1
        p = p[:i]
        if not p:
            break
    return p


def derive(items):
    """从官方序列号条目推导机型前缀，返回 [(prefix, kind)]。"""
    out = []
    for typ, val in items:
        if typ == "RANGE" and "-" in val:
            a, b = val.split("-", 1)
            p = common_prefix([a, b])
            if len(p) >= MIN_PREFIX:
                out.append((p, "区间"))
    if out:
        return out
    vals = [v for _, v in items if v]
    if not vals:
        return []
    if len(vals) == 1:
        return [(vals[0][:8], "单条")]
    p = common_prefix(vals)
    if len(p) >= MIN_PREFIX:
        return [(p, "列表公共前缀")]
    cnt = collections.Counter(v[:8] for v in vals)
    top, n = cnt.most_common(1)[0]
    if n >= len(vals) * 0.6:
        return [(top, "列表众数 %d/%d" % (n, len(vals)))]
    return []


def range_starts(items):
    """RANGE 起点本身的前 8 位（官方号段内更细的型号段，作为合法截短）。"""
    out = []
    for typ, val in items:
        if typ == "RANGE" and "-" in val:
            a = val.split("-", 1)[0]
            if len(a) >= 8:
                out.append((a[:8], "区间起点"))
    return out


def pick_raw(items, px):
    """取与该前缀最贴近的一条官方原始值（区间优先），用于文档里展示出处。"""
    ranges = [v for t, v in items if t == "RANGE" and v.startswith(px)]
    if ranges:
        return ranges[0]
    for t, v in items:
        if v.startswith(px):
            return v
    return items[0][1] if items else ""


def candidates(recs, model):
    """某官方型号的全部可用前缀 (prefix, kind, trackingNumber, 原始值)。"""
    out = []
    for tn, r in recs.items():
        if r["model"] == model:
            for p, k in derive(r["items"]) + range_starts(r["items"]):
                if p not in [x[0] for x in out]:
                    out.append((p, k, tn, pick_raw(r["items"], p)))
    return out


# ---------------------------------------------------------------- 已核实条目
# (分组, 前缀, 页面显示名, FAA 官方型号, 备注)
CURATED = [
    # ---- 消费级 ----
    ("消费级", "1581F0M6", "Mavic 2 Zoom", "Mavic 2 Zoom", ""),
    ("消费级", "1581F11V", "Phantom 4 Pro V2.0", "Phantom 4 Pro V2.0", ""),
    ("消费级", "1581F163", "Mavic 2 Pro", "Mavic 2 Pro", ""),
    ("消费级", "1581F1WN", "Mavic Air 2", "Mavic Air 2 (MA2UE1N)", "官方区分 MA2UE1N / MA2UE3W"),
    ("消费级", "1581F3N3", "Mavic Air 2", "Mavic Air 2 (MA2UE3W)", ""),
    ("消费级", "1581F3YT", "Air 2S", "Air 2S", ""),
    ("消费级", "1581F45T", "Mavic 3", "Mavic 3", ""),
    ("消费级", "1581F4QZ", "Mavic 3 Cine", "Mavic 3 Cine", ""),
    ("消费级", "1581F5YH", "Mini 3", "Mini 3", ""),
    ("消费级", "1581F67P", "Mavic 3 Classic", "Mavic 3 Classic", ""),
    ("消费级", "1581F67Q", "Mavic 3 Pro", "Mavic 3 Pro", ""),
    ("消费级", "1581F6L5", "Mini 3 Pro", "Mini 3 Pro", "官方号段 1581F6L5…"),
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
    ("植保", "1581F446", "Agras T10", "Agras T10", "T10 / T30 同号段"),
    ("植保", "1581F446", "Agras T30", "Agras T30", ""),
    ("植保", "1581F574", "Agras T20P", "Agras T20P", "T20P / T40 同号段"),
    ("植保", "1581F574", "Agras T40", "Agras T40", ""),
    ("植保", "1581F6BU", "Agras T50", "Agras T50 & T25", "官方含 T25 别名"),
    ("植保", "1581F836", "Agras T25P", "Agras T25P", ""),
    ("植保", "1581F8ZL", "Agras T100", "Agras T100", ""),
    ("植保", "1581F8ZX", "Agras T70P", "Agras T70P", ""),
    # ---- 物流 ----
    ("物流", "1581F7C6", "FlyCart 30", "FlyCart 30", ""),
    ("物流", "1581FAN4", "FlyCart 100", "FlyCart 100", ""),
    # ---- 其他厂商 ----
    ("其他厂商", "1748FEV4AL", "Autel Alpha", "Autel Alpha", ""),
    ("其他厂商", "1748FDF2JD2924", "Dragonfish Lite", "Dragonfish Lite", "官方按单台序列号登记"),
    ("其他厂商", "1748FDF3JD3", "Dragonfish Pro", "Dragonfish Pro", ""),
    ("其他厂商", "1748FDF1JD1", "Dragonfish Standard", "Dragonfish Standard", ""),
    ("其他厂商", "1748CHL7", "EVO II", "EVO II", "官方号段 1748CHL78…"),
    ("其他厂商", "1748FEV2H", "EVO II V3", "EVO II V3", ""),
    ("其他厂商", "1748CLTC", "EVO Lite", "EVO Lite", "与 EVO Lite+ 共用号段"),
    ("其他厂商", "1748CLT09214", "EVO Lite", "EVO Lite", ""),
    ("其他厂商", "1748CLTG9214910", "EVO Lite", "EVO Lite", ""),
    ("其他厂商", "1748CLTC", "EVO Lite+", "EVO Lite+", ""),
    ("其他厂商", "1748CNA", "EVO Nano", "EVO Nano", ""),
    ("其他厂商", "1748FEC4NAC825", "EVO Nano+", "EVO Nano+", ""),
    ("其他厂商", "1748FEV3HM", "EVO Max", "EVO Max", ""),
    ("其他厂商", "1748FEV3HMP", "MDX-1", "MDX-1", ""),
    ("其他厂商", "1588E040452", "Anafi AI", "ANAFI Ai", "…452 = AI，…505 = USA"),
    ("其他厂商", "1588E040505", "Anafi USA", "ANAFI USA", ""),
    ("其他厂商", "1588E040465", "Anafi UKR GOV", "PARROT ANAFI UKR GOV", ""),
    ("其他厂商", "1668F00", "Skydio 2", "2 SDRC2V1", "官方只有 5 位公共段"),
    ("其他厂商", "1668BS20", "Skydio 2+", "2+ SDR35V1", ""),
    ("其他厂商", "1668BE10JA00", "Skydio X2E", "X2E SDR21V1", ""),
    ("其他厂商", "1668BR40EA00", "Skydio X10E", "X10E SR47P", ""),
    ("其他厂商", "1668BR70CA00UZ", "Skydio R10", "R10 SDR5PC", ""),
    ("其他厂商", "1863FPEG8218", "XAG P100", "P100", ""),
    ("其他厂商", "1863FPEG8219", "XAG P100 Pro", "P100 Pro", ""),
    ("其他厂商", "1863FPEG8223", "XAG P150", "P150", ""),
    ("其他厂商", "1863F251", "XAG P150 Max", "P150 Max", ""),
    ("其他厂商", "1863F242", "XAG P60", "P60", ""),
    ("其他厂商", "1644FMD4100000007", "md4-1000", "md4-1000", ""),
    ("其他厂商", "1644FMD4300000001", "md4-3000", "md4-3000", ""),
    ("其他厂商", "1644FMD4E1VA0000", "EasyOne", "EasyOne", ""),
    ("其他厂商", "1587A051", "eBee X", "eBee X series", "051000 = eBee X"),
    ("其他厂商", "1587A540", "eBee TAC", "eBee X series", "054000 = eBee TAC"),
    ("其他厂商", "1701DSPR", "Percepto AirMax", "Percepto AirMax", ""),
    ("其他厂商", "1701DGAS", "Percepto AirMax OGI", "AirMax OGI", ""),
    ("其他厂商", "17444242", "WingtraOne Gen II", "WingtraOne Gen II", "官方区间 174442423~174449999"),
]

GROUP_ORDER = ["消费级", "穿越机", "行业级", "植保", "物流", "其他厂商"]


def validate(recs):
    """逐条核对 CURATED：前缀必须能在官方序列号里找到出处。"""
    errs = []
    for grp, px, disp, official, note in CURATED:
        cands = candidates(recs, official)
        if not cands:
            errs.append("官方无该型号的序列号登记: %s (%s)" % (official, disp))
            continue
        ok = False
        for c, kind, tn, raw in cands:
            if px == c:
                ok = True
            elif c.startswith(px) and len(px) >= MIN_PREFIX:
                ok = True
            elif px.startswith(c) and len(c) >= MIN_PREFIX:
                ok = True
        if not ok:
            errs.append("前缀无官方依据: %s %s -> %s 候选=%s"
                        % (px, disp, official, [c[0] for c in cands][:4]))
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
    toks[-1] = "]]"
    toks.append("];")
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


# ---------------------------------------------------------------- 第三方库比对
def parse_draft(path=DRAFT):
    rows, sec = [], None
    for line in io.open(path, encoding="utf-8").read().splitlines():
        m = re.match(r"^【(.+?)】共", line)
        if m:
            sec = m.group(1)
            continue
        m = re.match(r"^  ([0-9A-Za-z]+) +(.+?)\s*$", line)
        if m and sec:
            rows.append({"sec": sec, "px": m.group(1), "name": m.group(2)})
    return rows


ALIAS = {
    "Mavic 2 Ent. Advanced": "Mavic 2 Enterprise Advanced",
    "Matrice 300 RTK": "M300 RTK", "Matrice 350 RTK": "M350 RTK",
    "Matrice 30 (机场版)": "M30 Dock version",
    "Matrice 30T (机场版)": "M30T Dock version", "经纬M30": "M30",
    "Mavic 3E": "M3E", "Mavic 3T": "M3T", "Mavic 3M": "M3M",
    "Matrice 3D": "M3D", "Matrice 3TD": "M3TD", "Matrice 3TA": "M3TA",
    "Mavic 4 Pro (64GB)": "Mavic 4 Pro (64 GB)",
    "Mavic 4 Pro (512GB)": "Mavic 4 Pro (512 GB)",
    "Agras T10 / T30": "Agras T10", "Agras T20P / T40": "Agras T20P",
    "Agras T50": "Agras T50 & T25", "EVO Max / MDX-1": "EVO Max",
    "Avata 360 (DVN3NT)": "DJI Avata 360 (DVN3NT)",
    # Skydio 官方型号名里带硬件代号，草稿只写商品名
    "Skydio 2 / 2+": "2 SDRC2V1", "Skydio X2E": "X2E SDR21V1",
    "Skydio X2": "2+ SDR35V1", "Skydio X10": "X10E SR47P",
    "Anafi": "ANAFI Ai", "Anafi USA": "ANAFI USA",
    "eBee X": "eBee X series", "eBee TAC": "eBee X series",
    "eBee Geo": "eBee X series", "eBee RTK": "eBee X series",
}

VENDOR_WORDS = ("dji", "skydio", "parrot", "autel", "xag", "yuneec", "freefly",
                "sensefly", "ageagle", "microdrones", "wingtra", "percepto", "swellpro")


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def model_score(name, official):
    """草稿型号名与官方型号名的相似度，0 表示对不上。

    只做保守匹配：完全一致，或包含关系且较短一方至少 6 个字符、长度差不超过 6，
    避免 'Mini 2 SE' 被 'Mini' 这种短名吸走。
    """
    if name.strip().lower() == official.strip().lower():
        return 100
    a, b = norm(name), norm(official)
    if not a or not b:
        return 0
    if a == b:
        # 'EVO Nano' 与 'EVO Nano+' 去掉标点后相同，按原文是否完全一致区分
        return 95
    short, long_ = (a, b) if len(a) <= len(b) else (b, a)
    if len(short) >= 6 and len(long_) - len(short) <= 6 and short in long_:
        return 60
    # 去掉厂商前缀再比（Skydio X2E -> X2E）
    toks = [t for t in re.split(r"\s+", name) if t]
    while toks and toks[0].lower() in VENDOR_WORDS:
        toks = toks[1:]
    a2 = norm(" ".join(toks))
    if a2 and a2 != a:
        s2, l2 = (a2, b) if len(a2) <= len(b) else (b, a2)
        if len(s2) >= 6 and len(l2) - len(s2) <= 6 and s2 in l2:
            return 50
    return 0


def draft_report(recs):
    """草稿库逐条判定：已核实 / 前缀可截短 / 前缀需修正 / 官方无登记。

    判定顺序：先看草稿前缀本身能否在官方数据里找到出处（最硬的证据），
    再看型号名能否对上（处理「官方型号名带括号别名」这类情况）。
    """
    px_index = []          # (prefix, kind, model, tracking)
    raw_map = []           # (raw_serial, model) 用于判断草稿前缀是否落在具体序列号里
    for tn, r in recs.items():
        for p, k in derive(r["items"]) + range_starts(r["items"]):
            px_index.append((p, k, r["model"], tn))
        for typ, val in r["items"]:
            for part in val.split("-"):
                if len(part) >= 6:
                    raw_map.append((part, r["model"]))
    models = sorted({r["model"] for r in recs.values() if r["model"]})

    known_bad = {u[1] for u in UNVERIFIED}
    rows = []
    for d in parse_draft():
        px = d["px"]
        if px in known_bad:
            rows.append(dict(d, verdict="官方无登记", official="—", off="—"))
            continue

        # 1) 前缀反查：草稿前缀是否等于/短于某条官方前缀
        exact = [x for x in px_index if x[0] == px]
        shorter = [x for x in px_index if x[0].startswith(px) and len(px) >= MIN_PREFIX]
        longer = [x for x in px_index if px.startswith(x[0]) and len(x[0]) >= MIN_PREFIX]

        # 2) 型号名匹配
        want = ALIAS.get(d["name"], d["name"])
        scored = sorted(((model_score(want, m), m) for m in models), reverse=True)
        best_model = scored[0][1] if scored and scored[0][0] else None

        if exact or shorter:
            hit = exact or shorter
            # 若型号名也能对上，优先取同名的记录
            if best_model:
                same_model = [x for x in hit if x[2] == best_model]
                hit = same_model or hit
            official = hit[0][2]
            order = ([x for x in hit if x[0].startswith(px)] +
                     [x for x in hit if not x[0].startswith(px)])
            off = "、".join(list(dict.fromkeys(x[0] for x in order))[:2])
            v = "已核实" if exact else "前缀可截短"
        elif longer and not best_model:
            # 草稿比官方更细，且型号名对不上 -> 官方没有这一段
            official = longer[0][2]
            off = "、".join(list(dict.fromkeys(x[0] for x in longer))[:2])
            v = "前缀需修正"
        elif best_model:
            cands = candidates(recs, best_model)
            if not cands:
                rows.append(dict(d, verdict="官方无登记", official=best_model, off="—"))
                continue
            official = best_model
            order = ([c for c in cands if c[0].startswith(px)] +
                     [c for c in cands if not c[0].startswith(px)])
            off = "、".join(list(dict.fromkeys(c[0] for c in order))[:2])
            # 草稿前缀若正好是某条具体序列号的开头，也算可用
            in_raw = any(raw.startswith(px) and m == best_model and len(px) >= 8
                         for raw, m in raw_map)
            v = "前缀可截短" if in_raw else "前缀需修正"
        else:
            rows.append(dict(d, verdict="官方无登记", official="—", off="—"))
            continue

        rows.append(dict(d, verdict=v, official=official, off=off))
    return rows


# ---------------------------------------------------------------- 主流程
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--doc", action="store_true")
    ap.add_argument("--suggest", action="store_true")
    ap.add_argument("--index", action="store_true")
    ap.add_argument("--review", action="store_true")
    args = ap.parse_args()

    recs = parse_serial()

    if args.suggest:
        want = sys.argv[2] if len(sys.argv) > 2 else ""
        agg = collections.defaultdict(list)
        for tn, r in recs.items():
            if want and want.lower() not in (r["make"] or "").lower():
                continue
            for c, kind in derive(r["items"]) + range_starts(r["items"]):
                key = (r["make"], r["model"])
                if c not in [x[0] for x in agg[key]]:
                    agg[key].append((c, kind))
        for (mk, md), lst in sorted(agg.items()):
            print("%-22s %-36s %s" % (mk[:22], md[:36],
                                      " | ".join("%s(%s)" % (p, k) for p, k in lst)))
        return 0

    errs = validate(recs)
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
            print("机型表与官方快照一致（%d 条）" % len(CURATED))
        else:
            print("机型表已漂移，请运行 --write")
            return 1
    if args.review:
        write_review(recs)
        print("已生成 %s" % REVIEW)
    if args.index:
        write_index(recs)
        print("已生成 %s" % INDEX)
    if args.doc:
        write_doc(recs)
        print("已生成 %s" % DOC)
    return 0


# ---------------------------------------------------------------- 核对版机型库
def write_review(recs):
    """草稿库逐条核对结果（可机读），判定同 docs/model_db.md 第四节。"""
    rows = draft_report(recs)
    out = ["# 机型库核对版（草稿库 vs FAA 官方序列号）",
           "# 源 data/models_draft_2026-11-11.txt | 共 %d 条" % len(rows),
           "# 判定：已核实 = 前缀与官方一致；前缀可截短 = 是官方号段的前段；",
           "#       前缀需修正 = 官方有该型号但前缀对不上；官方无登记 = 官方清单查无此型号",
           "# 字段: 分类\t草稿前缀\t草稿型号\t判定\t官方前缀\t官方型号"]
    for r in rows:
        out.append("\t".join([r["sec"], r["px"], r["name"], r["verdict"],
                              r["off"].replace("—", ""), r["official"].replace("—", "")]))
    io.open(REVIEW, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    return len(rows)


# ---------------------------------------------------------------- 前缀索引
def write_index(recs):
    """输出全量前缀索引：去重、按长度分层、一对多展开。"""
    seen = {}
    for tn, r in recs.items():
        for p, k in derive(r["items"]) + range_starts(r["items"]):
            key = (p, r["make"], r["model"])
            if key in seen:
                continue
            seen[key] = (k, tn)
    rows = []
    for (p, make, model), (k, tn) in seen.items():
        n = len(p)
        tier = "精确" if n >= 12 else ("可用" if n >= 8 else "慎用")
        rows.append((tier, n, p, k, make, model, tn))
    rows.sort(key=lambda x: ({"精确": 0, "可用": 1, "慎用": 2}[x[0]], -x[1], x[4], x[5]))
    cnt = collections.Counter()
    for r in rows:
        cnt[(r[2], r[5])] += 1
    multi = {p for (p, _) in cnt if cnt[(p, _)] > 1}
    out = ["# FAA 机型前缀索引（由官方序列号推导）",
           "# 数据源 data/faa_serial_%s.tsv | 共 %d 条" % (SNAPSHOT_DATE, len(rows)),
           "# 分层：精确 = 前缀 >=12 位；可用 = 8~11 位；慎用 = 5~7 位（会大面积误命中）",
           "# 字段: 分层\t前缀长度\t前缀\t推导方式\t厂商\t型号\t声明编号"]
    for tier, n, p, k, make, model, tn in rows:
        out.append("\t".join([tier, str(n), p, k, make, model, tn]))
    io.open(INDEX, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    return len(rows)


# ---------------------------------------------------------------- 文档
UNVERIFIED = [
    ("消费级", "1581F6CD", "Mini 2 SE", "DJI 未登记该型号的 DoC"),
    ("消费级", "1581F8PJ", "Mini 4K", "DJI 未登记该型号的 DoC"),
    ("消费级", "1581FB34", "Lito X1", "无任何厂商登记"),
    ("消费级", "1581FBV5", "Lito 1", "无任何厂商登记"),
    ("消费级", "1778S000", "SplashDrone 4", "SwellPro 未登记该型号"),
    ("消费级", "PF723000", "Rolling Spider", "Parrot 未登记"),
    ("消费级", "PF727078", "Mambo FLY", "Parrot 未登记"),
    ("消费级", "YUNQ4KPUS", "Q500 4K Typhoon", "Yuneec 全品牌未登记"),
    ("穿越机", "1581F7V2", "Flip", "DJI 未登记该型号的 DoC"),
    ("穿越机", "1581F87L", "Neo", "DJI 未登记该型号的 DoC"),
    ("穿越机", "1581FA6Q", "Neo 2", "DJI 未登记该型号的 DoC"),
    ("行业级", "1701A000", "Percepto Max", "官方型号是 AirMax / OGI"),
    ("行业级", "1701A001", "Percepto Sparrow", "官方只有 AirMax / OGI"),
    ("行业级", "1701A002", "Percepto Air", "官方只有 AirMax / OGI"),
    ("行业级", "YUNH5ETWET", "H520 Hexacopter", "Yuneec 未登记"),
    ("行业级", "YUNH920202EU", "Tornado H920", "Yuneec 未登记"),
    ("行业级", "YUNTYH3EU", "Typhoon H3", "Yuneec 未登记"),
    ("植保", "1581FAE3", "Agras T70S", "DJI 只登记 T70P"),
    ("植保", "1758X003", "V40", "XAG 无该型号（有 P60 / P100 / P150）"),
    ("电影级", "18179000", "Alta X", "Freefly 登记了型号但无序列号"),
    ("电影级", "18179002", "Alta 8", "Freefly 未登记"),
    ("测绘级", "1587A003", "eBee RTK", "官方 eBee X series 不含 RTK"),
    ("测绘级", "1688W001", "WingtraOne GEN I", "Wingtra 只登记 Gen II / RAY"),
]


def _official_for(recs, official, px=""):
    cands = candidates(recs, official)
    if px:
        narrowed = [c for c in cands if c[0].startswith(px) or px.startswith(c[0])]
        cands = narrowed or cands
    return cands[:2]


def write_doc(recs):
    os.makedirs(os.path.dirname(DOC), exist_ok=True)
    n_all = len(CURATED)
    n_px = len({c[1] for c in CURATED})
    rep = draft_report(recs)
    cnt = collections.Counter(r["verdict"] for r in rep)
    L = []
    A = L.append
    A("# 机型库可信清单（FAA UAS DoC 官方序列号）")
    A("")
    A("> 数据源：FAA UAS Declaration of Compliance（<https://uasdoc.faa.gov>），序列号快照 "
      "`data/faa_serial_%s.tsv`（516 条声明 / 5004 条序列号条目）。" % SNAPSHOT_DATE)
    A("> 本文档与固件 `main/crid_web.c` 的机型预设表由 `python tools/models_faa.py --doc` 从同一份快照生成，不要手工改表。")
    A("")
    A("## 一、什么算「已核实」")
    A("")
    A("官方登记的是**序列号本身**，不是一个「前缀字段」。所以本表的前缀一律由序列号推导，规则固定：")
    A("")
    A("| 官方登记形态 | 推导方式 | 例子 |")
    A("| --- | --- | --- |")
    A("| `RANGE`（起止区间） | 取起点与终点的公共前缀 | `1581F45T000000000000-1581F45TZZZZZZZZZZZZ` → `1581F45T` |")
    A("| 序列号列表 | 全部值取公共前缀；不足 5 位时取前 8 位众数（占比 ≥60%） | `1668F002…`/`1668F000…` → `1668F00` |")
    A("| 单条序列号 | 取前 8 位，标记低置信 | `1587A540002APD` → `1587A540` |")
    A("| `RANGE` 起点 | 起点前 8 位作为合法截短（官方号段内更细的型号段） | `1748CHL7822390001` → `1748CHL7` |")
    A("")
    A("反过来，下面这些一律不算，不进固件：官方没登记该型号、官方登记了型号但没有序列号、")
    A("把型号代号当序列号前缀（`SDR21V1`、`SR47PCV`）、「厂商码 + 000/001 递增」这类凭空构造。")
    A("")
    A("## 二、结果")
    A("")
    A("| 判定 | 条数 |")
    A("| --- | --- |")
    A("| ✅ 已核实，已进入固件 | **%d**（%d 个前缀） |" % (n_all, n_px))
    A("| ⚠️ 第三方草稿库中前缀需修正 | %d |" % cnt.get("前缀需修正", 0))
    A("| ⚠️ 第三方草稿库中前缀可截短使用 | %d |" % cnt.get("前缀可截短", 0))
    A("| ❌ 官方无该型号登记 | %d |" % cnt.get("官方无登记", 0))
    A("")
    A("草稿库共 %d 条：%d 条与官方完全一致，%d 条前缀需替换为官方值，%d 条前缀可截短，%d 条查无此型号。"
      % (len(rep), cnt.get("已核实", 0), cnt.get("前缀需修正", 0),
         cnt.get("前缀可截短", 0), cnt.get("官方无登记", 0)))
    A("")
    A("## 三、已核实清单（固件当前使用的全部条目）")
    A("")
    for grp in GROUP_ORDER:
        items = [c for c in CURATED if c[0] == grp]
        if not items:
            continue
        A("### %s（%d 条）" % (grp, len(items)))
        A("")
        A("| 前缀 | 显示名 | FAA 官方型号 | 官方原始登记 | 备注 |")
        A("| --- | --- | --- | --- | --- |")
        for _, px, disp, official, note in items:
            off = _official_for(recs, official, px)
            if off:
                c, k, tn, raw = off[0]
                shown = "%s（%s）" % (raw, k)
            else:
                shown = "—"
            A("| `%s` | %s | %s | `%s` | %s |" % (px, disp, official, shown, note or ""))
        A("")
    A("## 四、第三方草稿库逐条核对")
    A("")
    A("`data/models_draft_2026-11-11.txt`（108 条）与官方序列号逐条比对。**「前缀需修正」的条目，"
      "草稿里的前缀在官方数据里找不到任何出处**，用它会生成查无此机的序列号。")
    A("")
    A("| 分类 | 草稿前缀 | 草稿型号 | 官方前缀 | 官方型号 | 判定 |")
    A("| --- | --- | --- | --- | --- | --- |")
    for r in rep:
        if r["verdict"] in ("已核实", "官方无登记"):
            continue
        A("| %s | `%s` | %s | `%s` | %s | %s |"
          % (r["sec"], r["px"], r["name"], r["off"], r["official"], r["verdict"]))
    A("")
    A("## 五、官方清单中不存在的型号（UNVERIFIED）")
    A("")
    A("这些条目只保留在文档里备查，**不进固件、不参与任何匹配**。缺失原因多为该机型未在美国登记 DoC，")
    A("不代表机器不存在，但在拿到官方依据前不应给它们编前缀。")
    A("")
    A("| 分类 | 草稿前缀 | 型号 | 说明 |")
    A("| --- | --- | --- | --- |")
    whys = {u[1]: u[3] for u in UNVERIFIED}
    for r in rep:
        if r["verdict"] != "官方无登记":
            continue
        A("| %s | `%s` | %s | %s |" % (r["sec"], r["px"], r["name"],
                                       whys.get(r["px"], "官方清单无此型号的可匹配记录")))
    A("")
    A("## 六、共用一个前缀的情况")
    A("")
    A("官方数据里确实存在「一个号段对应两个型号」，固件保留多条记录，选型时随机取前缀：")
    A("")
    A("| 前缀 | 涉及型号 | 处理 |")
    A("| --- | --- | --- |")
    A("| `1581F446` | Agras T10 / T30 | 两条记录共用，型号名由使用者在预设里选 |")
    A("| `1581F574` | Agras T20P / T40 | 同上 |")
    A("| `1748CLTC` | Autel EVO Lite / EVO Lite+ | 官方同号段，两条记录共用 |")
    A("| `1581F4BN` / `1581F5BK` | M30 / M30T | 官方分开登记，固件也分开 |")
    A("| `1588E040…` | Parrot Anafi AI / USA / UKR GOV | 只差第 10-12 位，必须用 11 位前缀区分 |")
    A("")
    A("## 七、数据怎么抓的")
    A("")
    A("```")
    A("GET /api/v1/publicDOCRev?itemsPerPage=500&pageIndex=0                  # 516 条声明清单")
    A("GET /api/v1/publicDOCRev/{trackingNumber}                             # 取 publicRevision.docRevId")
    A("GET /api/v1/serialNumbers?findBy=docRevId&docRevId={id}               # 该声明登记的序列号")
    A("```")
    A("")
    A("（主机名 `https://uasdoc.faa.gov`，公开接口，无需登录。")
    A("官方逐位编码说明摘录在 `data/faa_serial_notes_2026-11-11.txt`，例如 eBee 的 `051000 = eBee X /")
    A("052000 = eBee GEO / 054000 = eBee TAC`，本表拆分 eBee 型号即依据这段说明。）")
    A("")
    A("注意：`publicRevision.serialNumberStart/End` 字段只对 102 条声明有值，且部分已过期")
    A("（Mini 3 Pro 的 start 字段仍是 `1581F4XF…`，序列号表里已改为 `1581F6L5…`）。")
    A("**判定一律以 `serialNumbers` 为准**，这也是官网详情页「Serial Numbers」弹窗展示的内容。")
    A("")
    A("## 八、维护")
    A("")
    A("```bash")
    A("python tools/models_faa.py --check     # 回归：表是否与快照一致")
    A("python tools/models_faa.py --suggest   # 看官方给某厂商推导出的前缀（可加厂商名过滤）")
    A("python tools/models_faa.py --write     # 改完 CURATED 后同步进 crid_web.c")
    A("python tools/models_faa.py --doc       # 重新生成本文档")
    A("python tools/models_faa.py --review    # 草稿库核对结果 -> data/models_reviewed_*.tsv")
    A("python tools/models_faa.py --index     # 全量前缀索引 -> data/faa_prefix_index_*.tsv")
    A("```")
    A("")
    A("换快照时：重新抓一遍序列号写入 `data/faa_serial_YYYY-MM-DD.tsv`，改脚本里的 `SERIAL`/`SNAPSHOT_DATE`，")
    A("先跑 `--check` 看哪些条目失效，再决定是保留旧前缀还是随官方更新。")
    A("")
    io.open(DOC, "w", encoding="utf-8", newline="\n").write("\n".join(L))


if __name__ == "__main__":
    sys.exit(main())
