// 临时诊断：在 https 上下文里跑真实页面（含真实设备响应），看字段是否被填上
process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';
const fs = require('fs');
const https = require('https');
const { JSDOM } = require('jsdom');

const html = fs.readFileSync(process.argv[2], 'utf8')
  .replace(/<link[^>]*>/g, '')
  .replace(/<script src=[^>]*><\/script>/g, '');

const PROTO = process.argv[3] || 'https';

function realFetch(u, opt) {
  const path = typeof u === 'string' ? u : (u && u.url) || '';
  const method = (opt && opt.method) || 'GET';
  const mod = PROTO === 'https' ? https : require('http');
  return new Promise((res, rej) => {
    const req = mod.request({
      host: '192.168.4.1', port: PROTO === 'https' ? 443 : 80,
      path, method, rejectUnauthorized: false,
      headers: Object.assign({}, (opt && opt.headers) || {}),
    }, (r) => {
      let b = '';
      r.on('data', (c) => (b += c));
      r.on('end', () => res({ ok: r.statusCode === 200, status: r.statusCode,
        json: async () => JSON.parse(b), text: async () => b }));
    });
    req.on('error', rej);
    if (opt && opt.body) req.write(opt.body);
    req.end();
  });
}

const dom = new JSDOM(html, {
  url: PROTO + '://192.168.4.1/',
  runScripts: 'dangerously',
  pretendToBeVisual: true,
  beforeParse(window) {
    window.fetch = (u, o) => realFetch(u, o).catch((e) => {
      window._fetchErr = (window._fetchErr || []).concat([String(u) + ' -> ' + e.message]);
      throw e;
    });
    window._pageErrors = [];
    window._msgs = [];
    window.addEventListener('error', (e) => window._pageErrors.push(e.message));
    const mk = (o) => Object.assign({
      addTo(){return this;}, setLatLng(){return this;}, getLatLng(){return {lat:0,lng:0};},
      setStyle(){}, setLatLngs(){}, removeLayer(){}, bindPopup(){return this;},
      openPopup(){}, on(){return this;}, setView(){return this;}, fitBounds(){return this;},
      getZoom(){return 13;}, remove(){}, invalidateSize(){},
    }, o || {});
    window.L = { map(){return mk({});}, tileLayer(){return mk();}, circleMarker(){return mk();},
      marker(){return mk();}, polyline(){return mk();}, circle(){return mk();},
      latLngBounds(){return {pad(){return {};}};}, divIcon(){return {};} };
  },
});

const w = dom.window;
setTimeout(() => {
  console.log('=== ' + PROTO + ' ===');
  console.log('顶层错误:', w._pageErrors.length ? w._pageErrors : '(none)');
  console.log('fetch 失败:', w._fetchErr && w._fetchErr.length ? w._fetchErr : '(none)');
  const mt = w.document.getElementById('msg');
  console.log('页面提示:', mt ? JSON.stringify(mt.textContent) : '(no #msg)');
  ['uas_id','ssid','channel','beacon_interval_ms','base_latitude','speed_horizontal','patrol_speed']
    .forEach((id) => {
      const el = w.document.getElementById(id);
      console.log('  #' + id + ' =', el ? JSON.stringify(el.value) : 'MISSING');
    });
  process.exit(0);
}, 2500);
