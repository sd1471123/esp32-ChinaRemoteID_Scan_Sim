# 串口日志抓取（用于验证复位/网页并发之类的现场问题）
# 用法: python tools/ser_capture.py -p COM5 -t 30 -o ser.log [--reset]
# 说明: 必须用 IDF 的 python env 运行（自带 pyserial）：
#       C:\Users\pc1\.espressif\python_env\idf6.1_py3.12_env\Scripts\python.exe
import argparse
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-p', '--port', default='COM5')
    ap.add_argument('-b', '--baud', type=int, default=115200)
    ap.add_argument('-t', '--seconds', type=float, default=20)
    ap.add_argument('-o', '--out', default='ser.log')
    ap.add_argument('--reset', action='store_true',
                    help='抓之前用 RTS 复位一次（等价于按 EN）')
    a = ap.parse_args()

    s = serial.Serial(a.port, a.baud, timeout=0.2)
    if a.reset:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.15)
        s.setRTS(False)

    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < a.seconds:
        chunk = s.read(8192)
        if chunk:
            buf += chunk
    s.close()

    with open(a.out, 'wb') as f:
        f.write(buf)
    print('captured %d bytes -> %s' % (len(buf), a.out))


if __name__ == '__main__':
    main()
