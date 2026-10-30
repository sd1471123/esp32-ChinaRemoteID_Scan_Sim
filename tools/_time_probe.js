// 授时状态条专项探针：确认「授时成功/失败」在页面上真的有差别可见。
// 用法：TMODE=ok|fail node tools/_time_probe.js
const fs = require('fs');
const { JSDOM } = require(process.env.JSDOM_PATH || 'jsdom');

const MODE = process.env.TMODE || 'ok';
const html = fs.readFileSync(__dirname + '/_page.html', 'utf8')
  .replace(/<link[^>]*>/g, '')
  .replace(/<script src=[^>]*><\/script>/g, '');

const nowSec = Math.floor(Date.now() / 1000);

const dom = new JSDOM(html, {
  url: 'http://192.168.4.1/',
  runScripts: 'dangerously',
  pretendToBeVisual: true,
  beforeParse(window) {
    window.fetch = async (u, opt) => {
      const s = String(u);
      const m = (opt && opt.method) || 'GET';
      if (s.indexOf('/api/time') === 0) {
        if (MODE === 'fail') return { json: async () => ({ ok: false, now: 0 }) };
        return { json: async () => ({ ok: true, now: nowSec }) };
      }
      if (s.indexOf('/api/status') === 0) {
        return { json: async () => ({
          latitude: 22.5, longitude: 114.0, altitude_msl: 50, heading: 90, speed: 1,
          heap: 70000, hmin: 40000, hblk: 30000,
          tsync: MODE === 'fail' ? 0 : 1,
          tnow: MODE === 'fail' ? 0 : nowSec,
        })};
      }
      if (s.indexOf('/api/config') === 0 && m === 'POST') return { json: async () => ({ ok: true }) };
      if (s.indexOf('/api/config') === 0) return { json: async () => ({
        device_mode: 0, rid_standard: 0, uas_id: 'X', drone_name: 'X',
        ssid: 'ESP32-CRID-TEST', ap_password: 'x', channel: '6',
        beacon_interval_ms: 200, ua_type: '2', base_altitude_msl: 50,
        ground_altitude: 0, altitude_agl: 50, altitude_wave: 5, baro_altitude: 50,
        heading: 45, heading_mode: 0, speed_vertical: 0, vspd_mode: 0,
        height_type: 0, operator_location_type: 1,
        band: 0, tx_2g: 1, rx_2g: 0, tx_5g: 1, rx_5g: 0,
        channel_5g: 149, has5g: true,
      })};
      if (s.indexOf('/api/log/info') === 0) return { json: async () => ({
        on: MODE === 'fail' ? 0 : 1, count: 0, used: 0, cap: 4000000, targets: [],
      })};
      if (s.indexOf('/api/log/enable') === 0) return { json: async () => ({
        ok: true, on: (opt && JSON.parse(opt.body || '{}').on) || 0,
      })};
      if (s.indexOf('/api/ota/info') === 0) return { json: async () => ({
        version: 'test', size: 0, md5: '', compile_time: '', page_ver: 'test',
      })};
      if (s.indexOf('/api/rx') === 0) return { json: async () => ({ stats: {}, tracks: [] })};
      return { json: async () => ({ ok: true }) };
    };
    window._errors = [];
    window.addEventListener('error', (e) => window._errors.push(e.message));
  },
});

setTimeout(() => {
  const w = dom.window;
  const d = w.document;
  const el = d.getElementById('tstat');
  const flow = d.getElementById('tstat') ? 'present' : 'MISSING';
  console.log('MODE       :', MODE);
  console.log('tstat 元素 :', flow);
  if (el) {
    console.log('class      :', JSON.stringify(el.className));
    console.log('文案       :', el.textContent);
  }
  const m = d.getElementById('msg');
  console.log('提示行     :', m ? JSON.stringify(m.textContent) : '(none)');
  // 记录开关是否被自动打开（授时成功的连带效果）
  const sw = d.getElementById('lgsw');
  console.log('记录开关   :', sw ? sw.textContent : '(none)');
  console.log('顶层错误   :', w._errors.length ? w._errors : '(none)');
  dom.window.close();
}, 1500);
