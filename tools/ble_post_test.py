# BLE POST 链路自测（PC 侧，模拟 App 的写配置流程）
#
# 用法: python tools/ble_post_test.py [--name ESP32-CRID] [--post]
#   不加 --post 只做 GET /api/config（验证桥接基本可用）
#   加  --post 则把读到的配置原样 POST 回去（验证 0x12/0x13 分帧 + HTTPS 自连）
#
# 说明: 依赖 bleak（Windows 上走 WinRT，需要系统蓝牙打开）。
#       帧格式与 android BleClient.kt 保持一致，任何一边改动都要同步。

import argparse
import asyncio
import json
import sys

from bleak import BleakClient, BleakScanner

SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


class Link:
    def __init__(self, client):
        self.c = client
        self.buf = bytearray()
        self.armed = False
        self.expected = -1
        self.done = None
        self.err = None

    def on_notify(self, _ch, data: bytearray):
        if not data:
            return
        t = data[0]
        if t == 0x01 and len(data) >= 5:
            self.expected = int.from_bytes(data[1:5], "big")
            self.buf = bytearray()
            self.armed = True
        elif t == 0x02 and self.armed:
            self.buf += data[1:]
            if self.expected > 0 and len(self.buf) >= self.expected:
                self.armed = False
                self._finish()
        elif t == 0x03 and self.armed:
            self.armed = False
            self._finish()
        elif t == 0x04:
            if self.armed:
                self.armed = False
                self.err = "设备错误码 %d" % (data[1] if len(data) > 1 else 0)
                if self.done and not self.done.done():
                    self.done.set_result(None)

    def _finish(self):
        if self.done and not self.done.done():
            self.done.set_result(None)

    async def _wait(self, timeout):
        self.done = asyncio.get_event_loop().create_future()
        try:
            await asyncio.wait_for(self.done, timeout)
        except asyncio.TimeoutError:
            raise RuntimeError("蓝牙响应超时")
        if self.err:
            raise RuntimeError(self.err)
        return self.buf.decode("utf-8", "replace")

    async def get(self, path, timeout=8):
        self.buf = bytearray()
        self.expected = -1
        self.armed = False
        self.err = None
        await self.c.write_gatt_char(RX, path.encode(), response=True)
        return await self._wait(timeout)

    async def post(self, path, body, timeout=20):
        pb = path.encode()
        bb = body.encode()
        chunk = max(20, min((self.c.mtu_size or 23) - 3, 244))
        self.buf = bytearray()
        self.expected = -1
        self.armed = False
        self.err = None

        head = bytearray([0x12, len(pb), len(bb) & 0xFF, (len(bb) >> 8) & 0xFF]) + pb
        off = 0
        room = chunk - len(head)
        if room > 0 and bb:
            off = min(room, len(bb))
            head += bb[:off]
        await self.c.write_gatt_char(RX, bytes(head), response=True)

        while off < len(bb):
            n = min(chunk - 1, len(bb) - off)
            await self.c.write_gatt_char(RX, bytes([0x13]) + bb[off:off + n], response=True)
            off += n
        return await self._wait(timeout)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="ESP32-CRID")
    ap.add_argument("--post", action="store_true")
    ap.add_argument("--body", default=None, help="直接指定请求体（默认把读到的配置回写）")
    ap.add_argument("--pad", type=int, default=0, help="用 N 字节填充造一条长请求体")
    ap.add_argument("--addr", default=None)
    a = ap.parse_args()

    if a.addr:
        addr = a.addr
    else:
        print("扫描 %s* ..." % a.name)
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: (ad.local_name or "").startswith(a.name), timeout=15
        )
        if dev is None:
            print("没扫到设备")
            return 1
        addr = dev.address
        print("找到 %s (%s)" % (dev.name, addr))

    async with BleakClient(addr, timeout=25) as c:
        print("已连接，MTU=%s" % c.mtu_size)
        link = Link(c)
        await c.start_notify(TX, link.on_notify)

        cfg = await link.get("/api/config")
        print("GET /api/config -> %d 字节" % len(cfg))
        try:
            print(json.dumps(json.loads(cfg), ensure_ascii=False)[:300])
        except Exception as e:
            print("（回包不是合法 JSON，PC 侧通知有丢帧，不影响下面的 POST 测试: %s）" % e)

        if a.post:
            if a.body:
                body = a.body
            elif a.pad:
                # 造一条足够长的合法 JSON（未知键会被忽略，长度用来压测分帧）
                body = '{"_pad":"' + "x" * a.pad + '"}'
            else:
                body = json.dumps(json.loads(cfg), ensure_ascii=False)
            print("POST /api/config 请求体 %d 字节" % len(body))
            try:
                r = await link.post("/api/config", body)
                print("POST -> %s" % r)
            except Exception as e:
                print("POST 失败: %s" % e)
                return 2
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()) or 0)
