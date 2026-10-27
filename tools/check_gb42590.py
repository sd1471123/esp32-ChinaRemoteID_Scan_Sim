#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GB 42590-2023 (旧国标) 广播式远程识别报文校验器

用途：完全复刻 main/crid_messages.c 的编码逻辑，然后用国标附录 A
（表 A.1 / A.4~A.10、表 A.9 编解码方式）的解码规则反解，逐字段比对
"发送想表达的值" 与 "第三方接收端解出来的值"。

依据：GB 42590-2023 附录 A《远程识别要求》（A.1.2.2 / A.1.3）
"""
import struct
import sys

# ---------------- 待发送的业务值（与固件默认配置一致） ----------------
CFG = dict(
    uas_id="ESP32-CRID-4BD4",
    id_type=1,          # 1 = 设备序列号
    ua_type=2,          # 2 = 直升飞机(多旋翼)
    lat=23.14287,
    lon=113.26026,
    alt_msl=50.0,
    alt_agl=50.0,
    speed_h=1.0,
    speed_v=0.0,
    heading=45.0,
    status=2,           # 2 = 空中
    height_type=0,      # 0 = 基于起飞地
    op_lat=23.14,
    op_lon=113.26,
    op_alt=10.0,
    op_loc_type=1,      # 1 = 动态位置
    classification_type=0,
    category_eu=2,      # 国标: UA 运行类别（表 A.7 的 7-4 位）
    class_eu=1,         # 国标: UA 等级（表 A.7 的 3-0 位）
    drone_name="ESP32S3",
    operator_id="ESP32-OP-4BD4",
    ts_ms=1761529200 * 1000,   # 任意固定时刻 (UTC 2025-10-27 03:00:00)
)

MESSAGE_SIZE = 25


# ---------------- 编码：与 crid_messages.c 逐行对应 ----------------
def enc_ground_speed(v):
    """crid_messages.c: encode_ground_speed（GB 42590 表 A.9）"""
    if v < 0:
        v = 0
    if v < 63.75:
        return int(v / 0.25 + 0.5)
    elif v <= 254.25:
        return int((v - 63.75) / 0.75 + 0.5)
    return 254


def enc_altitude(a):
    if a < -1000:
        a = -1000
    v = int((a + 1000.0) / 0.5)
    return max(0, min(65535, v))


def basic_id():
    m = bytearray(MESSAGE_SIZE)
    m[0] = (0x0 << 4) | 0x01
    m[1] = (CFG['ua_type'] & 0x0F) | ((CFG['id_type'] & 0x0F) << 4)
    uid = CFG['uas_id'].encode()[:20]
    m[2:2 + len(uid)] = uid
    return m


def location():
    m = bytearray(MESSAGE_SIZE)
    m[0] = (0x1 << 4) | 0x01
    speed_mult = 1 if CFG['speed_h'] >= 63.75 else 0
    # 航迹角：无效值上报 361°（EW=1 且编码值 181），见 GB 42590 表 A.9
    if CFG['heading'] < 0 or CFG['heading'] >= 360:
        ew, track = 1, 181
    elif CFG['heading'] >= 180:
        ew, track = 1, min(179, int(CFG['heading'] - 180 + 0.5))
    else:
        ew, track = 0, min(179, int(CFG['heading'] + 0.5))
    m[1] = ((CFG['status'] & 0x0F) << 4) | ((CFG['height_type'] & 0x01) << 2) \
           | (ew << 1) | speed_mult
    m[2] = track
    m[3] = enc_ground_speed(CFG['speed_h'])
    # 垂直速度：8 位有符号，code = v / 0.5，饱和 ±124
    vs = int(CFG['speed_v'] * 2.0)
    m[4] = max(-124, min(124, vs)) & 0xFF
    struct.pack_into('<i', m, 5, int(CFG['lat'] * 1e7))
    struct.pack_into('<i', m, 9, int(CFG['lon'] * 1e7))
    struct.pack_into('<H', m, 13, enc_altitude(CFG['alt_msl']))   # 气压高度
    struct.pack_into('<H', m, 15, enc_altitude(CFG['alt_msl']))   # 几何高度
    struct.pack_into('<H', m, 17, enc_altitude(CFG['alt_agl']))   # 距地高度
    m[19] = (0x04 << 4) | 0x0B
    m[20] = (0x00 << 4) | 0x04
    sec = (CFG['ts_ms'] // 1000) % 3600           # 自整点起秒数
    struct.pack_into('<H', m, 21, sec * 10 + (CFG['ts_ms'] % 1000) // 100)
    m[23] = (0x00 << 4) | 0x02
    return m


def self_id():
    m = bytearray(MESSAGE_SIZE)
    m[0] = 0x01 | (0x3 << 4)
    m[1] = 0
    d = CFG['drone_name'].encode()[:23]
    m[2:2 + len(d)] = d
    return m


def system():
    m = bytearray(MESSAGE_SIZE)
    m[0] = (0x4 << 4) | 0x01
    m[1] = ((CFG['classification_type'] & 0x07) << 2) | (CFG['op_loc_type'] & 0x03)
    struct.pack_into('<i', m, 2, int(CFG['op_lat'] * 1e7))
    struct.pack_into('<i', m, 6, int(CFG['op_lon'] * 1e7))
    struct.pack_into('<H', m, 10, 1)
    m[12] = 10            # 运行区域半径 1 LSB = 10 m → 100 m
    struct.pack_into('<H', m, 13, enc_altitude(100.0))
    struct.pack_into('<H', m, 15, enc_altitude(50.0))
    # 表 A.7: 7-4 位 = UA 运行类别，3-0 位 = UA 等级
    m[17] = ((CFG['category_eu'] & 0x0F) << 4) | (CFG['class_eu'] & 0x0F)
    struct.pack_into('<H', m, 18, enc_altitude(CFG['op_alt']))
    struct.pack_into('<I', m, 20, (CFG['ts_ms'] // 1000) - 1546300800)
    return m


def operator_id():
    m = bytearray(MESSAGE_SIZE)
    m[0] = 0x01 | (0x5 << 4)
    m[1] = 0x00
    d = CFG['operator_id'].encode()[:20]
    m[2:2 + len(d)] = d
    return m


def build_ie(counter=0):
    msgs = [basic_id(), location(), self_id(), system(), operator_id()]
    pack = bytes([0xF1, MESSAGE_SIZE, len(msgs)]) + b''.join(bytes(x) for x in msgs)
    ie_len = 4 + 1 + len(pack)          # OUI(3)+Type(1)+Counter(1)+Message
    ie = bytes([0xDD, ie_len, 0xFA, 0x0B, 0xBC, 0x0D, counter]) + pack
    return ie


# ---------------- 解码：按 GB 42590-2023 表 A.9 / A.1.3 ----------------
def dec_ground_speed(code, mult):
    if code == 255:
        return None                      # 无效
    return code * 0.25 if mult == 0 else code * 0.75 + 63.75


def dec_altitude(code):
    v = code * 0.5 - 1000.0
    return None if code == 0 else v


def parse(buf):
    """模拟第三方检测设备的解析过程"""
    if buf[0] != 0xDD:
        return None, "Element ID 不是 0xDD"
    ln = buf[1]
    if buf[2:5] != b'\xFA\x0B\xBC':
        return None, "OUI 不匹配"
    if buf[5] != 0x0D:
        return None, "Vend Type 不是 0x0D"
    counter = buf[6]
    pk = buf[7:7 + ln - 5]
    if (pk[0] >> 4) != 0x0F:
        return None, "打包头报文类型不是 0xF"
    ver = pk[0] & 0x0F
    single = pk[1]
    count = pk[2]
    if single != 25:
        return None, "报文长度不是 25"
    if 3 + count * single > len(pk):
        return None, "报文数量与长度不符"
    out = {'counter': counter, 'version': ver, 'count': count, 'msgs': {}}
    for i in range(count):
        m = pk[3 + i * single: 3 + (i + 1) * single]
        t = m[0] >> 4
        out['msgs'].setdefault(t, m)
    return out, None


def report(ie):
    print("=" * 72)
    print("Wi-Fi Vendor IE（%d 字节）" % len(ie))
    print("=" * 72)
    print(' '.join('%02X' % b for b in ie))
    print("  [DD][%02X] ← Element ID / Len(=OUI 起算的 %d 字节)" % (ie[1], ie[1]))
    print("  [FA 0B BC] OUI  [%02X] Vend Type  [%02X] Message Counter" % (ie[5], ie[6]))
    print("  [%02X] 打包头(类型F/版本%d)  [%02X] 报文长度  [%02X] 报文数量"
          % (ie[7], ie[7] & 0x0F, ie[8], ie[9]))
    print()

    r, err = parse(ie)
    if err:
        print("!! 设备解析失败: %s" % err)
        return

    checks = []

    def chk(name, got, want, ok=None, skip=False):
        if skip:
            checks.append((name, got, want, None))
            return
        if ok is None:
            ok = (got == want)
        checks.append((name, got, want, ok))

    print("── 外层 ──")
    chk("打包头报文类型", 0x0F, 0x0F, (ie[7] >> 4) == 0x0F)
    chk("协议版本号", r['version'], 1)
    chk("单条报文长度", ie[8], 25)
    chk("报文数量", r['count'], 5)

    # --- 基本ID ---
    b = r['msgs'].get(0)
    if b:
        print("\n── 0x0 基本ID报文 ──")
        idt, uat = b[1] >> 4, b[1] & 0x0F
        chk("报文头(类型0/版本1)", b[0], 0x01)
        chk("ID 类型(7-4位)", idt, CFG['id_type'])
        chk("UA 类型(3-0位)", uat, CFG['ua_type'])
        uid = b[2:22].rstrip(b'\x00').decode('ascii', 'replace')
        chk("UAS ID", uid, CFG['uas_id'])

    # --- 位置向量 ---
    l = r['msgs'].get(1)
    if l:
        print("\n── 0x1 位置向量报文 ──")
        status = l[1] >> 4
        height_type = (l[1] >> 2) & 1
        ew = (l[1] >> 1) & 1
        mult = l[1] & 1
        chk("运行状态", status, CFG['status'])
        chk("高度类型", height_type, CFG['height_type'])
        chk("航迹角", l[2] + (180 if ew else 0), CFG['heading'])
        gs = dec_ground_speed(l[3], mult)
        chk("地速 (m/s)", None if gs is None else round(gs, 2), CFG['speed_h'],
            gs is not None and abs(gs - CFG['speed_h']) < 0.13)
        vs_enc = l[4]
        vs_dec = vs_enc - 256 if vs_enc > 127 else vs_enc      # 8 位有符号
        vs_val = vs_dec * 0.5
        chk("垂直速度 (m/s)", vs_val, CFG['speed_v'],
            abs(vs_val - CFG['speed_v']) < 0.5)
        lat = struct.unpack_from('<i', l, 5)[0] / 1e7
        lon = struct.unpack_from('<i', l, 9)[0] / 1e7
        chk("纬度", round(lat, 7), round(CFG['lat'], 7), abs(lat - CFG['lat']) <= 2e-7)
        chk("经度", round(lon, 7), round(CFG['lon'], 7))
        chk("气压高度 (m)", dec_altitude(struct.unpack_from('<H', l, 13)[0]), CFG['alt_msl'])
        chk("几何高度 (m)", dec_altitude(struct.unpack_from('<H', l, 15)[0]), CFG['alt_msl'])
        chk("距地高度 (m)", dec_altitude(struct.unpack_from('<H', l, 17)[0]), CFG['alt_agl'])
        chk("水平精度 NACp", l[19] & 0x0F, 11)
        chk("垂直精度 GVA", l[19] >> 4, 4)
        chk("速度精度 NACv", l[20] & 0x0F, 4)
        ts = struct.unpack_from('<H', l, 21)[0]
        want_ts = ((CFG['ts_ms'] // 1000) % 3600) * 10 + (CFG['ts_ms'] % 1000) // 100
        chk("时间戳 (0.1s/整点)", ts, want_ts)

    # --- 运行描述 ---
    s = r['msgs'].get(3)
    if s:
        print("\n── 0x3 运行描述报文 ──")
        chk("报文头", s[0], 0x31)
        chk("描述类型", s[1], 0)
        desc = s[2:25].rstrip(b'\x00').decode('ascii', 'replace')
        chk("描述文本", desc, CFG['drone_name'])

    # --- 系统报文 ---
    y = r['msgs'].get(4)
    if y:
        print("\n── 0x4 系统报文 ──")
        chk("报文头", y[0], 0x41)
        region = (y[1] >> 2) & 0x07
        loc_type = y[1] & 0x03
        chk("操作员位置类型(1-0位)", loc_type, CFG['op_loc_type'])
        # 「等级分类归属区域」按需求保留现状，不按标准 2（中国）改写
        chk("等级分类归属区域(4-2位)", region, 2, skip=True)
        op_lat = struct.unpack_from('<i', y, 2)[0] / 1e7
        op_lon = struct.unpack_from('<i', y, 6)[0] / 1e7
        chk("操作员纬度", round(op_lat, 7), round(CFG['op_lat'], 7))
        chk("操作员经度", round(op_lon, 7), round(CFG['op_lon'], 7))
        chk("运行区域计数", struct.unpack_from('<H', y, 10)[0], 1)
        chk("运行区域半径 (m)", y[12] * 10, 100.0)   # 1 LSB = 10 m
        cat = y[17] >> 4          # 7-4位 = UA 运行类别
        lvl = y[17] & 0x0F        # 3-0位 = UA 等级
        chk("UA 运行类别(7-4位)", cat, CFG['category_eu'])
        chk("UA 等级(3-0位)", lvl, CFG['class_eu'])
        chk("操作员高度 (m)", dec_altitude(struct.unpack_from('<H', y, 18)[0]), CFG['op_alt'])
        ts19 = struct.unpack_from('<I', y, 20)[0] + 1546300800
        chk("系统时间戳 (Unix秒)", ts19, CFG['ts_ms'] // 1000)

    # --- 运行人ID ---
    o = r['msgs'].get(5)
    if o:
        print("\n── 0x5 运行人ID报文 ──")
        chk("报文头", o[0], 0x51)
        chk("运行人ID类型", o[1], 0)
        oid = o[2:22].rstrip(b'\x00').decode('ascii', 'replace')
        chk("运行人ID", oid, CFG['operator_id'])

    print("\n" + "=" * 72)
    print("%-26s %-18s %-18s %s" % ("检查项", "设备解出的值", "发送意图", "结果"))
    print("-" * 72)
    bad = 0
    skipped = 0
    for name, got, want, ok in checks:
        if ok is None:
            skipped += 1
            verdict = "SKIP(按需求保留)"
        elif ok:
            verdict = "OK"
        else:
            bad += 1
            verdict = "FAIL"
        print("%-26s %-18s %-18s %s" % (name, got, want, verdict))
    print("-" * 72)
    print("合计 %d 项，通过 %d 项，保留 %d 项，失败 %d 项"
          % (len(checks), len(checks) - bad - skipped, skipped, bad))
    return bad


if __name__ == '__main__':
    ie = build_ie(0)
    bad = report(ie)
    print()
    if bad:
        print("结论：存在 %d 处与 GB 42590-2023 附录 A 不一致" % bad)
    else:
        print("结论：全部字段符合 GB 42590-2023 附录 A")

    # ---------------- 地速全量程扫描（表 A.9 编码规则） ----------------
    print("\n" + "=" * 72)
    print("地速编码扫描（标准：<=63.75 时 code=v/0.25；>63.75 时 code=(v-63.75)/0.75）")
    print("=" * 72)
    print("%-10s %-8s %-10s %-12s %-12s %s"
          % ("实际(m/s)", "mult", "编码值", "标准应编", "设备解出", "结果"))
    worst = 0
    for v in (1.0, 20.0, 63.0, 63.75, 64.0, 80.0, 100.0, 150.0, 200.0, 254.0):
        mult = 1 if v >= 63.75 else 0
        code = enc_ground_speed(v)
        if mult == 0:
            want = int(round(v / 0.25))
        elif v <= 254.25:
            want = int(round((v - 63.75) / 0.75))
        else:
            want = 254
        dec = dec_ground_speed(code, mult)
        err = 0 if dec is None else abs(dec - v)
        worst = max(worst, err)
        tol = 0.13 if mult == 0 else 0.38      # 量化半格：0.25/2 与 0.75/2
        print("%-10s %-8d %-10d %-12d %-12s %s"
              % (v, mult, code, want,
                 "无效" if dec is None else round(dec, 2),
                 "OK" if err <= tol else "FAIL(Δ%.2f)" % err))
    print("-" * 72)
    print("最大偏差 %.2f m/s（标准分辨率 0.25/0.75 m/s）" % worst)

    # ---------------- 垂直速度扫描 ----------------
    print("\n" + "=" * 72)
    print("垂直速度编码扫描（标准：8位有符号，code = v/0.5）")
    print("=" * 72)
    print("%-12s %-10s %-14s %s" % ("实际(m/s)", "编码值", "设备解出(m/s)", "结果"))
    for v in (0.0, 1.0, 2.0, -1.0, -2.0, 5.0, 62.0, -62.0, 80.0, -80.0):
        code = max(-124, min(124, int(round(v * 2.0)))) & 0xFF
        signed = code - 256 if code > 127 else code
        dec = signed * 0.5
        if abs(v) > 62.0:
            verdict = "OK(超量程饱和到 ±62)" if abs(signed) == 124 else "FAIL(未饱和)"
        else:
            verdict = "OK" if abs(dec - v) < 0.5 else "FAIL(Δ%.1f)" % (dec - v)
        print("%-12s %-10d %-14s %s" % (v, signed, dec, verdict))
    sys.exit(1 if bad else 0)

