"""复位设备并抓取串口启动日志（原始字节直写，避免 PowerShell 管道按 GBK 解码导致中文乱码）。

用法:
    python tools/cap_boot.py COM3 [输出文件] [抓取秒数]

依赖 pyserial（IDF 环境自带的 python 里已安装）。
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
out = sys.argv[2] if len(sys.argv) > 2 else 'build_boot_%s.txt' % port
duration = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0

s = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.2)
s.dtr = False          # IO0 高 -> 正常启动
s.rts = True           # EN 低 -> 复位
time.sleep(0.15)
s.rts = False          # 释放
s.dtr = False

buf = bytearray()
end = time.time() + duration
while time.time() < end:
    d = s.read(8192)
    if d:
        buf += d
s.close()

with open(out, 'wb') as f:
    f.write(bytes(buf))
print('bytes=%d -> %s' % (len(buf), out))
