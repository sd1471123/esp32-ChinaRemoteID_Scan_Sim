"""重制 README 截图（docs/webui_tx.png / docs/webui_rx.png）。

做法：起一个本地 HTTP 服务，把 tools/_page.html 作为页面返回，并按真实接口格式
mock /api/config、/api/status、/api/rx、/api/time，再用 Edge headless 截图。
这样页面里的 fetch、轮询、卡片渲染、地图都走真实代码路径，不是 P 图。

需要联网加载 Leaflet CDN 与 OSM 瓦片（走本地代理，见 PROXY）。
用法：
    python tools/gen_shots.py            # 两张都重制
    python tools/gen_shots.py tx         # 只重制发射模式
"""
import http.server
import json
import os
import socketserver
import subprocess
import sys
import threading
import time
from urllib.parse import urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, 'tools', '_page.html')
RXMAP = os.path.join(ROOT, 'main', 'rxmap_page.js')
DOCS = os.path.join(ROOT, 'docs')
EDGE = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
PROXY = os.environ.get('SHOT_PROXY', 'http://127.0.0.1:7897')

STATE = {'mode': 'tx'}

CFG_TX = {
    'device_mode': 0,
    'rid_standard': 1,               # 1 = GB 46750 新国标
    'uas_id': '1581F5QJDHDSQA00B1',
    'drone_name': 'DJI Air 3S',
    'operator_id': 'ESP32-CRID-4BD4',
    'realname_id': 'CN00AB12',
    'ssid': 'ESP32-CRID-4BD4',
    'channel': 6,
    'beacon_interval_ms': 200,
    'ua_type': 2,
    'ua_category_gb': 1,
    'base_latitude': 22.540500,
    'base_longitude': 113.953500,
    'base_altitude_msl': 58.0,
    'altitude_agl': 120.0,
    'speed_horizontal': 8.5,
    'status': 2,
    'operator_latitude': 22.539800,
    'operator_longitude': 113.952000,
    'operator_altitude': 56.0,
    'patrol_speed': 1.2,
    'patrol_radius_lat': 0.0009,
    'patrol_radius_lon': 0.0012,
}

CFG_RX = dict(CFG_TX)
CFG_RX['device_mode'] = 1

NOW = int(time.time() * 1000)

TRACKS = [
    {
        'mac': '44:BD:8D:69:06:30', 'uas_id': '1581F5QJDHDSQA00B1',
        'drone_name': 'DJI Air 3S', 'protocol': 4, 'status': 2,
        'rssi': -52, 'channel': 6,
        'latitude': 22.541820, 'longitude': 113.954800,
        'altitude_geo': 120.5, 'altitude_agl': 78.2, 'altitude_baro': 118.0,
        'speed_horizontal': 8.5, 'speed_vertical': 0.4, 'direction': 137.0,
        'msg_count': 186, 'age_ms': 900, 'first_age_ms': 45200,
        'active': True, 'ua_type': 1, 'realname_id': 'CN00AB12',
        'has_op_loc': True, 'op_latitude': 22.539800, 'op_longitude': 113.952000,
        'op_altitude_geo': 56.0, 'operator_id': 'ESP32-CRID-4BD4',
        'op_loc_type': 1, 'coord_sys': 0, 'h_acc': 11, 'v_acc': 5,
        'spd_acc': 3, 'ts_unix_ms': NOW - 900, 'ts_acc': 4, 'agl_type': 2,
    },
    {
        'mac': '44:BD:8D:12:AB:77', 'uas_id': '1596F3B2C8D4E5A60071',
        'drone_name': 'DJI Mavic 3 Pro', 'protocol': 3, 'status': 2,
        'rssi': -67, 'channel': 6,
        'latitude': 22.538650, 'longitude': 113.956200,
        'altitude_geo': 96.0, 'altitude_agl': 60.0, 'altitude_baro': 94.0,
        'speed_horizontal': 12.3, 'speed_vertical': -0.5, 'direction': 268.0,
        'msg_count': 142, 'age_ms': 1500, 'first_age_ms': 31200,
        'active': True, 'ua_type': 2,
        'has_op_loc': True, 'op_latitude': 22.537200, 'op_longitude': 113.953100,
        'op_altitude_geo': 38.0, 'operator_id': 'CN-OP-8823',
        'ts_rel_10s': 1234, 'h_acc': 10, 'v_acc': 4, 'spd_acc': 2, 'agl_type': 1,
    },
    {
        'mac': '44:BD:8D:5C:2E:19', 'uas_id': '1470A9F3B6C21D880042',
        'drone_name': '', 'protocol': 0, 'status': 2,
        'rssi': -78, 'channel': 6,
        'latitude': 22.543100, 'longitude': 113.950200,
        'altitude_geo': 152.0, 'altitude_agl': 100.0,
        'speed_horizontal': 5.1, 'speed_vertical': 0.0, 'direction': 45.0,
        'msg_count': 96, 'age_ms': 2400, 'first_age_ms': 18900,
        'active': True, 'ua_type': 2,
        'has_op_loc': False, 'ts_rel_10s': 2400,
        'h_acc': 9, 'v_acc': 3, 'spd_acc': 2, 'agl_type': 1,
    },
    {
        'mac': '44:BD:8D:7F:31:02', 'uas_id': '1581F6K9M2P4R7T8V1W3',
        'drone_name': 'DJI Mini 4 Pro', 'protocol': 4, 'status': 1,
        'rssi': -88, 'channel': 6,
        'latitude': 22.535500, 'longitude': 113.948800,
        'altitude_geo': 40.0, 'altitude_agl': 5.0, 'altitude_baro': 39.0,
        'speed_horizontal': 0.0, 'speed_vertical': 0.0, 'direction': 0.0,
        'msg_count': 22, 'age_ms': 18400, 'first_age_ms': 96000,
        'active': False, 'ua_type': 1, 'realname_id': 'CN00CD34',
        'has_op_loc': True, 'op_latitude': 22.535100, 'op_longitude': 113.948300,
        'op_altitude_geo': 36.0, 'operator_id': 'CN-OP-1145',
        'op_loc_type': 1, 'coord_sys': 0, 'h_acc': 11, 'v_acc': 5,
        'spd_acc': 3, 'ts_unix_ms': NOW - 18400, 'ts_acc': 4, 'agl_type': 1,
    },
]

# 接收模式：让第一张卡片展开「详情」，其余保持折叠，体现折叠功能。
# pollRx 每 2s 会重建卡片 DOM，展开态会被覆盖，所以要持续保持。
INJECT_RX = """
<script>
window.addEventListener('load', function () {
  var n = 0;
  var t = setInterval(function () {
    var c = document.querySelector('#rxlist .dcard');
    if (c && !c.classList.contains('open')) c.classList.add('open');
    try { updFoldBtn(); } catch (e) {}
    if (++n > 40) clearInterval(t);
  }, 600);
});
</script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype):
        if isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ('/', '/index.html'):
            with open(PAGE, encoding='utf-8') as f:
                html = f.read()
            if STATE['mode'] == 'rx':
                html = html.replace('</body>', INJECT_RX + '</body>')
            self._send(200, html, 'text/html; charset=utf-8')
        elif path == '/api/config':
            cfg = CFG_RX if STATE['mode'] == 'rx' else CFG_TX
            self._send(200, json.dumps(cfg), 'application/json')
        elif path == '/api/status':
            self._send(200, json.dumps({
                'latitude': 22.541236, 'longitude': 113.954702,
                'altitude_msl': 178.40, 'heading': 137.0, 'mode': 0,
            }), 'application/json')
        elif path == '/api/rx':
            self._send(200, json.dumps({
                'tracks': TRACKS,
                'stats': {'rid': 4, 'total': 71, 'ok': 68, 'ovf': 0},
            }), 'application/json')
        elif path == '/rxmap.js':
            with open(RXMAP, 'rb') as f:
                self._send(200, f.read(), 'application/javascript')
        elif path == '/favicon.ico':
            self._send(204, b'', 'image/x-icon')
        else:
            self._send(404, '{}', 'application/json')

    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0)
        if n:
            self.rfile.read(n)
        self._send(200, json.dumps({'ok': True}), 'application/json')


def shoot(port, mode, out_name, height, width=1160):
    STATE['mode'] = mode
    out = os.path.join(DOCS, out_name)
    profile = os.path.join(ROOT, 'build', '_shot_profile_%s' % mode)
    cmd = [
        EDGE,
        '--headless=new',
        '--disable-gpu',
        '--hide-scrollbars',
        '--no-first-run',
        '--disable-extensions',
        '--user-data-dir=' + profile,
        '--proxy-server=' + PROXY,
        '--window-size=%d,%d' % (width, height),
        '--virtual-time-budget=25000',
        '--screenshot=' + out,
        'http://127.0.0.1:%d/' % port,
    ]
    print('>>> %s -> %s' % (mode, out))
    subprocess.run(cmd, check=False, timeout=180)
    if os.path.exists(out):
        print('    %d bytes' % os.path.getsize(out))
    else:
        print('    !! 截图未生成')


def main():
    want = sys.argv[1:] or ['tx', 'rx']
    httpd = socketserver.TCPServer(('127.0.0.1', 0), Handler)
    httpd.allow_reuse_address = True
    port = httpd.server_address[1]
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print('mock server on 127.0.0.1:%d' % port)
    try:
        if 'tx' in want:
            shoot(port, 'tx', 'webui_tx.png', 1960)
        if 'rx' in want:
            shoot(port, 'rx', 'webui_rx.png', 1560, width=1000)
    finally:
        httpd.shutdown()


if __name__ == '__main__':
    main()
