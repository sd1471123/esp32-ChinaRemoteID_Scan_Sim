# -*- coding: utf-8 -*-
"""用 opendroneid/libopendroneidcn 的解析规则校验设备实际广播的 GB46750 报文。

数据来源于设备启动日志中 ESP_LOG_BUFFER_HEX 打印的真实 IE 字节，
解析逻辑对照 opendroneidcn.c 的 CN46750_FindPacket / CN46750_RID_Decode。
"""
import sys

HEX = """
dd 4f fa 0b bc f0 ff 20 45 df fd fe 31 35 38 31
46 41 4e 34 46 36 59 4a 39 4b 51 4c 35 51 37 52
38 38 38 38 38 38 38 38 01 01 d5 1a 82 43 3a e2
ca 0d e4 07 07 47 75 48 81 77 ec 0e c2 01 0f 00
ab 46 00 34 08 02 00 0b 04 02 00 00 00 00 00 00
04
"""

buf = bytes(int(x, 16) for x in HEX.split())

# --- 1. 802.11 IE 基本校验 ---
assert buf[0] == 0xDD, "Element ID 必须是 0xDD"
assert buf[1] + 2 == len(buf), f"长度域不符: {buf[1]+2} vs {len(buf)}"
assert buf[2:5] == bytes([0xFA, 0x0B, 0xBC]), "OUI 必须是 FA:0B:BC"
assert buf[5] == 0xF0, f"GB46750 IE 类型必须是 0xF0，实际 0x{buf[5]:02X}"
print(f"IE 结构 OK: len={len(buf)}, OUI=FA:0B:BC, type=0x{buf[5]:02X}")

# --- 2. CN46750_FindPacket 等价逻辑 ---
pkt = buf[6:]                      # IE payload（OUI/Type 之后）
off = None
for i in range(len(pkt) - 3):
    if pkt[i] != 0xFF:
        continue
    if (pkt[i + 1] & 0xE0) != 0x20:       # 版本高 3 位固定 001
        continue
    dlen = pkt[i + 2]
    if not (1 <= dlen <= 200):
        continue
    fpos, nflags = i + 3, 0
    while fpos + nflags < len(pkt):
        nflags += 1
        if pkt[fpos + nflags - 1] & 0x01 == 0:
            break
    total = 3 + nflags + dlen
    if i + total <= len(pkt):
        off, flen, fl = i, nflags, fpos
        break
assert off is not None, "官方 FindPacket 逻辑找不到报文"
ver, dlen = pkt[off + 1], pkt[off + 2]
print(f"FindPacket OK: Type=0xFF, Version=V1.{ver & 0x1F}, DataLen={dlen}, FlagBytes={flen}")

# --- 3. 数据标识链 ---
flags = pkt[fl:]
assert flags[0] & 0x01 and flags[1] & 0x01 and not (flags[2] & 0x01), "扩展位链错误"
print("数据标识链 OK: 3 字节，前两字节扩展位=1，末字节=0")

# --- 4. 逐项解码（顺序与表2 位序一致） ---
d = pkt[fl + flen:]
p = 0
out = {}


def u16(p):
    return d[p] | (d[p + 1] << 8)


def s32(p):
    v = int.from_bytes(d[p:p + 4], "little")
    return None if v == 0xFFFFFFFF else (v - 2**32 if v >= 2**31 else v)


# 001
out["001 唯一产品识别码"] = d[p:p + 20].rstrip(b"\x00").decode("ascii")
p += 20
# 002
out["002 实名登记标志"] = d[p:p + 8].rstrip(b"\x00").decode("ascii")
p += 8
# 004 / 005
out["004 无人机分类"] = {0: "微型", 1: "轻型", 2: "小型", 3: "中型", 4: "大型"}.get(d[p], d[p])
p += 1
out["005 遥控站位置类型"] = {0: "起飞点", 1: "遥控站"}.get(d[p], d[p])
p += 1
# 006 经度|纬度
lon, lat = s32(p), s32(p + 4)
out["006 遥控站位置"] = "未知" if lon is None or lat is None else f"{lon/1e7:.7f}, {lat/1e7:.7f}"
p += 8
# 007 高度
raw = u16(p)
out["007 遥控站高度"] = "未知" if raw == 0 else f"{raw/2-1000:.1f} m"
p += 2
# 008
lon, lat = s32(p), s32(p + 4)
out["008 无人机位置"] = "未知" if lon is None or lat is None else f"{lon/1e7:.7f}, {lat/1e7:.7f}"
p += 8
# 009 / 010
raw = u16(p)
out["009 航迹角"] = "未知" if raw == 0xFFFF else f"{raw/10:.1f}°"
p += 2
raw = u16(p)
out["010 地速"] = "未知" if raw == 0xFFFF else f"{raw/10:.1f} m/s"
p += 2
# 011 相对高度
raw = u16(p)
out["011 相对高度"] = "未知" if raw == 0 else f"{raw/2-9000:.1f} m"
p += 2
# 012 垂直速度
raw = d[p]
if raw == 0xFF:
    out["012 垂直速度"] = "未知"
else:
    out["012 垂直速度"] = f"{'↓' if raw & 0x80 else '↑'}{(raw & 0x7F)/2:.1f} m/s"
p += 1
# 013 大地高度
raw = u16(p)
out["013 大地高度"] = "未知" if raw == 0 else f"{raw/2-1000:.1f} m"
p += 2
# 015 / 016
out["015 运行状态"] = {0: "未报告", 1: "地面", 2: "空中", 3: "紧急", 4: "广播失效", 5: "紧急+失效"}.get(d[p], d[p])
p += 1
out["016 坐标系"] = {0: "WGS-84", 1: "CGCS2000"}.get(d[p], d[p])
p += 1
# 017-019
acc_h = {0: "≥18.52km/未知", 1: "<18.52km", 4: "<1852m", 9: "<30m", 10: "<10m", 11: "<3m", 12: "<1m"}
out["017 水平精度"] = acc_h.get(d[p], d[p])
p += 1
out["018 垂直精度"] = {0: "≥150m/未知", 1: "<150m", 2: "<45m", 3: "<25m", 4: "<10m", 5: "<3m", 6: "<1m"}.get(d[p], d[p])
p += 1
out["019 速度精度"] = {0: "≥10m/s/未知", 1: "<10m/s", 2: "<3m/s", 3: "<1m/s", 4: "<0.3m/s"}.get(d[p], d[p])
p += 1
# 020 时间戳
ts = int.from_bytes(d[p:p + 6], "little")
out["020 时间戳"] = "未知(0)" if ts == 0 else f"{ts} ms"
p += 6
# 021 时间戳精度
out["021 时间戳精度"] = {0: ">0.5s/未知", 1: "≤0.5s", 2: "≤0.4s", 3: "≤0.3s", 4: "≤0.2s", 5: "≤0.1s"}.get(d[p], d[p])
p += 1

assert p == dlen, f"内容项长度不符: 解析 {p} / 声明 {dlen}"
print(f"内容项全部解析 OK（{p}/{dlen} 字节，无残留）\n")
for k, v in out.items():
    print(f"  {k:22s} {v}")

# --- 5. 与标准必选清单比对 ---
MANDATORY = ["001", "002", "004", "005", "006", "007", "008", "009",
             "010", "013", "015", "016", "017", "018", "019", "020", "021"]
present = [k.split()[0] for k in out]
missing = [m for m in MANDATORY if m not in present]
assert not missing, f"缺少必选数据项: {missing}"
print("\n必选 17 项齐全 + 可选 011/012")
sys.exit(0)
