// jsdom 全页真实 DOM 测试：加载完整页面 -> 恢复初始 -> 保存 -> 切接收模式
const fs = require('fs');
const { JSDOM } = require(process.env.JSDOM_PATH || 'jsdom');

const html = fs.readFileSync(__dirname + '/_page.html', 'utf8');

// 去掉外链资源（leaflet css/js 由桩替代），只保留内联 script
const patched = html
  .replace(/<link[^>]*>/g, '')
  .replace(/<script src=[^>]*><\/script>/g, '');

const dom = new JSDOM(patched, {
  url: 'http://192.168.4.1/',
  runScripts: 'dangerously',
  pretendToBeVisual: true,
  beforeParse(window) {
    window.fetch = async (u, opt) => {
      if (u === '/api/config' && opt && opt.method === 'POST') {
        try { window._lastPost = JSON.parse(opt.body); } catch (e) { window._lastPost = null; }
        return { json: async () => ({ ok: true }) };   // 走进 save() 成功分支
      }
      if (u === '/api/config') return { json: async () => ({
        device_mode: 0, rid_standard: 0, uas_id: 'X', drone_name: 'X',
        ssid: 'ESP32-CRID-TEST', ap_password: 'sd1471123',
        channel: '6', beacon_interval_ms: 5, ua_type: '2',
        base_altitude_msl: 50, ground_altitude: 0, altitude_agl: 50, altitude_wave: 5,
        baro_altitude: 50, heading: 45, heading_mode: 0, speed_vertical: 0, vspd_mode: 0,
        height_type: 0, operator_location_type: 1,
        band: 0, tx_2g: 1, rx_2g: 0, tx_5g: 1, rx_5g: 0,
        channel_5g: 149, has5g: true,
      })};
      if (u === '/api/rx') return { json: async () => ({
        stats: { total: 10, rid: 8, ok: 8, ovf: 0 },
        tracks: [{
          mac: 'AA:BB:CC:DD:EE:01', rssi: -50, channel: 6, protocol: 4,
          uas_id: '1581F5QJ8TEST000001', drone_name: '', operator_id: '',
          realname_id: 'CN00TEST', ua_type: 2,
          latitude: 22.54, longitude: 114.05, altitude_geo: 50,
          speed_horizontal: 1, speed_vertical: 0, direction: 90,
          status: 2, msg_count: 3, age_ms: 100,
          has_op_loc: true, op_latitude: 22.53, op_longitude: 114.04, op_altitude_geo: 25,
        }],
      })};
      // OTA 信息接口（带 ?n= 轮询参数，用前缀匹配）
      if (String(u).indexOf('/api/ota/info') === 0) return { json: async () => ({
        ver: '2.0.0', part: 'factory', part_size: 2097152,
        max: 2097152, token: 'tk123456', ota: true,
      })};
      // 历史轨迹：统计 + 目标列表
      if (String(u).indexOf('/api/log/info') === 0) return { json: async () => ({
        ready: true, count: 1280, capacity: 65472,
        used: 86016, total: 4186112, dropped: 0,
        oldest_unix: 1761800000, oldest_tick: 1000,
        newest_unix: 1761806420, newest_tick: 6420000,
        targets: [
          { mac: 'AA:BB:CC:DD:EE:01', uid: '1581F5QJ8TEST', count: 760,
            oldest_unix: 1761800000, newest_unix: 1761806420, newest_tick: 6420000,
            lat: 22.54, lon: 114.05, alt: 50 },
          { mac: 'AA:BB:CC:DD:EE:02', uid: '1581F9HECBGY3', count: 520,
            oldest_unix: 1761800100, newest_unix: 1761806400, newest_tick: 6400000,
            lat: 22.55, lon: 114.06, alt: 80 },
        ],
      })};
      // 历史轨迹：某目标的轨迹点（带 offset 分页，一次 30 条）
      if (String(u).indexOf('/api/log/points') === 0) {
        const uo = new URL(String(u), 'http://device');
        const off = Number(uo.searchParams.get('offset') || 0);
        window._logReq = String(u);
        const pts = [];
        for (let i = 0; i < 30; i++) {
          pts.push({
            seq: 1000 - off - i, t: 1761806420 - (off + i) * 10,
            tick: 6420000 - (off + i) * 10000,
            lat: 22.5400 + (off + i) * 0.00002, lon: 114.0501 + (off + i) * 0.00003,
            alt: 50 + i, agl: 29 + i, spd_h: 5.4, spd_v: 0.1,
            track: 89.5, ua: 2, status: 2, proto: 4, rssi: -57,
          });
        }
        return { json: async () => ({
          total: 1280, matched: 760, offset: off, limit: 30,
          mac: 'AA:BB:CC:DD:EE:01', points: pts,
        }) };
      }
      if (String(u).indexOf('/api/log/clear') === 0) return { json: async () => ({ ok: true }) };
      return { json: async () => ({}) };
    };
    window.confirm = () => true;
    // XHR 桩：只服务于固件升级（记录请求头/请求体，回一个 ok 的 JSON）
    window._otaReq = null;
    window.XMLHttpRequest = function () {
      const self = {
        _hdr: {}, status: 0, responseText: '', timeout: 0,
        upload: { onprogress: null },
        open(m, u) { self.method = m; self.url = u; },
        setRequestHeader(k, v) { self._hdr[k] = v; },
        send(body) {
          window._otaReq = { method: self.method, url: self.url, headers: self._hdr, body };
          setTimeout(() => {
            const n = body.size || body.length || 0;
            if (self.upload.onprogress) {
              self.upload.onprogress({ lengthComputable: true, loaded: n, total: n });
            }
            self.status = 200;
            self.responseText = JSON.stringify({ ok: true, size: n });
            if (self.onload) self.onload();
          }, 30);
        },
      };
      return self;
    };
    // Leaflet 桩
    const mk = (o) => Object.assign({
      addTo(){ return this; }, setLatLng(){ return this; }, getLatLng(){ return {lat:0,lng:0}; },
      setStyle(){}, setLatLngs(){}, removeLayer(){}, bindPopup(){ return this; },
      openPopup(){}, on(){ return this; }, setView(){ return this; },
      fitBounds(){ return this; }, getZoom(){ return 13; }, remove(){},
    }, o || {});
    window.L = {
      map(){ return mk({}); },
      tileLayer(){ return mk(); },
      circleMarker(){ return mk(); }, marker(){ return mk(); },
      polyline(){ return mk(); }, circle(){ return mk(); },
      latLngBounds(){ return { pad(){ return {}; } }; },
      divIcon(){ return {}; },
    };
    window._pageErrors = [];
    window.addEventListener('error', e => window._pageErrors.push(e.message));
  },
});

const w = dom.window;
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

(async () => {
  await sleep(300);
  console.log('=== 页面顶层错误 ===');
  console.log(w._pageErrors.length ? w._pageErrors : '(none)');

  // 关键元素存在性
  ['dd','ddbtn','dd3','dd4','map','grid','rxbox','channel'].forEach(id => {
    const el = w.document.getElementById(id);
    if (!el) console.log('MISSING ELEMENT: #' + id);
  });

  // 模拟：恢复初始（resetCfg 内部会 load()）
  try { await w.resetCfg(); console.log('resetCfg OK'); }
  catch (e) { console.log('resetCfg THREW:', e.message); }
  await sleep(200);

  // 模拟：点击保存并应用
  try { await w.save(); console.log('save OK'); }
  catch (e) { console.log('save THREW:', e.message); }

  // 角色开关：出厂/加载默认应为「发射」，且两个 radio 只能有一个选中
  try {
    const rd = w.document.querySelectorAll('input[name=role]');
    const checked = Array.prototype.filter.call(rd, r => r.checked);
    console.log('role radios:', rd.length, 'checked:', checked.length,
                'value:', checked[0] && checked[0].value);
    if (rd.length !== 2 || checked.length !== 1) console.log('ROLE UI FAIL');
    if (checked[0] && checked[0].value !== 'tx') console.log('ROLE DEFAULT FAIL (expect tx)');
  } catch (e) { console.log('role check THREW:', e.message); }

  // 模拟：拨到接收（复现用户报的 "pollRx is not defined" 场景）
  try {
    const rx = w.document.querySelector('input[name=role][value=rx]');
    rx.checked = true;
    rx.dispatchEvent(new w.Event('change', { bubbles: true }));
    await sleep(400);
    const cards = w.document.getElementById('rxlist').children.length;
    const txCards = w.document.querySelector('.card.txonly').style.display;
    console.log('rx role OK, rx cards:', cards, '(expect >=1), tx cards display:',
                JSON.stringify(txCards), '(expect "none")');
    if (txCards !== 'none') console.log('ROLE SWITCH FAIL (tx cards still visible)');
  } catch (e) { console.log('rx role THREW:', e.message); }

  // 导航按钮：按目标实际坐标给出「导航到无人机 / 导航到飞手」，点击后弹地图选择层
  try {
    const btns = w.document.querySelectorAll('#rxlist .navbtn');
    const kinds = Array.prototype.map.call(btns, b => b.dataset.nav);
    console.log('nav buttons:', btns.length, JSON.stringify(kinds), '(expect 2: drone,op)');
    if (btns.length !== 2 || kinds.join(',') !== 'drone,op') console.log('NAV BTN FAIL');

    // 页面不再有常驻的地图下拉
    if (w.document.getElementById('navmap')) console.log('NAV STALE SELECT FAIL');

    // 原生 scheme（调起 App）：高德 amapuri://route/plan（dev=1 = 传的是 WGS-84）
    const sa = w.navScheme('drone', 22.54, 114.05, 'DJI Air 3S', 'amap');
    console.log('amap scheme:', sa);
    if (!/^amapuri:\/\/route\/plan\/\?/.test(sa)
        || !/dlat=22\.540000/.test(sa) || !/dlon=114\.050000/.test(sa)
        || !/dev=1/.test(sa) || !/t=0/.test(sa)) console.log('NAV AMAP SCHEME FAIL');

    // 百度 scheme（Android 浏览器用 bdapp://）：latlng 是 纬度,经度 + coord_type=wgs84
    const sb = w.navScheme('op', 22.53, 114.04, '', 'bd');
    console.log('baidu scheme:', sb);
    if (!/^bdapp:\/\/map\/direction\?/.test(sb)
        || !/latlng:22\.530000,114\.040000/.test(sb) || !/coord_type=wgs84/.test(sb)) {
      console.log('NAV BAIDU SCHEME FAIL');
    }

    // 百度在 Android 上两种协议头都要试（bdapp 失败再 baidumap）
    const sb2 = w.navSchemes('op', 22.53, 114.04, '', 'bd');
    console.log('baidu schemes:', sb2.length, sb2.map(s => s.slice(0, 26)).join(' | '));
    if (sb2.length !== 2 || !/^bdapp:/.test(sb2[0]) || !/^baidumap:/.test(sb2[1])) {
      console.log('NAV BAIDU CASCADE FAIL');
    }

    // iOS 走另一套 scheme（iosamap://path / baidumap://map/direction）
    let savedUA = null;
    try {
      savedUA = w.navigator.userAgent;
      Object.defineProperty(w.navigator, 'userAgent',
        { value: 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X)', configurable: true });
      const ia = w.navScheme('drone', 22.54, 114.05, '', 'amap');
      const ib = w.navScheme('drone', 22.54, 114.05, '', 'bd');
      console.log('ios amap:', ia.slice(0, 32), '| ios baidu:', ib.slice(0, 34));
      if (!/^iosamap:\/\/path\?/.test(ia) || !/^baidumap:\/\/map\/direction\?/.test(ib)) {
        console.log('NAV IOS SCHEME FAIL');
      }
    } catch (e) { console.log('ios UA switch skipped:', e.message); }
    finally {
      try {
        Object.defineProperty(w.navigator, 'userAgent', { value: savedUA, configurable: true });
      } catch (e) {}
    }

    // 目的地名称：用机型/固定文案，不能是 20 位序列号
    console.log('dest name drone:', w.navDestName('drone', 'DJI Air 3S'),
                '| empty:', w.navDestName('drone', ''), '/', w.navDestName('op', ''));
    if (w.navDestName('drone', '') !== '无人机') console.log('NAV NAME FALLBACK FAIL');

    // 网页兜底 URL：高德 to=经度,纬度,name；百度 destination=latlng:纬度,经度
    const ua = w.navUrl('drone', 22.54, 114.05, 'TEST', 'amap');
    console.log('amap web url:', ua);
    if (!/^https:\/\/uri\.amap\.com\/navigation\?to=114\.050000,22\.540000,/.test(ua)
        || !/coordinate=wgs84/.test(ua)) console.log('NAV AMAP URL FAIL');

    const ub = w.navUrl('op', 22.53, 114.04, 'TEST', 'bd');
    console.log('baidu web url:', ub);
    if (!/api\.map\.baidu\.com/.test(ub) || !/latlng:22\.530000,114\.040000/.test(ub)
        || !/coord_type=wgs84/.test(ub)) console.log('NAV BAIDU URL FAIL');

    // 点按钮：只弹选择层，不直接跳转，也不冒泡触发卡片地图聚焦
    const sheet = w.document.getElementById('navpick');
    let hit = null;
    const origTo = w.navTo;
    w.navTo = (k, la, lo, nm, prov) => { hit = [k, la, lo, prov]; };
    btns[0].onclick({ stopPropagation() {}, target: btns[0] });
    console.log('sheet open:', sheet.classList.contains('on'),
                '| title:', w.document.getElementById('navpick_t').textContent,
                '| coord:', w.document.getElementById('navpick_p').textContent);
    if (!sheet.classList.contains('on')) console.log('NAV SHEET FAIL');
    if (hit) console.log('NAV SHEET FAIL (jumped before choosing)');

    // 选高德 -> 带 prov=amap 调 navTo，并关闭弹层
    w.document.querySelector('#navpick .navopt[data-prov="amap"]')
      .onclick({ stopPropagation() {} });
    console.log('after amap pick ->', JSON.stringify(hit), '| sheet open:',
                sheet.classList.contains('on'), '(expect false)');
    if (!hit || hit[0] !== 'drone' || hit[3] !== 'amap' || sheet.classList.contains('on'))
      console.log('NAV PICK FAIL');

    // 选百度同样透传 prov
    btns[1].onclick({ stopPropagation() {}, target: btns[1] });
    w.document.querySelector('#navpick .navopt[data-prov="bd"]')
      .onclick({ stopPropagation() {} });
    console.log('after bd pick ->', JSON.stringify(hit));
    if (!hit || hit[0] !== 'op' || hit[3] !== 'bd') console.log('NAV BD PICK FAIL');
    w.navTo = origTo;

    // 未上报坐标（0,0）必须拒绝导航
    if (w.posOk(0, 0) || !w.posOk(22.54, 114.05)) console.log('NAV POSOK FAIL');
  } catch (e) { console.log('nav check THREW:', e.message); }

  // 信道自由可选：切到 5.8G 后输入框不再禁用，范围 149-165，默认沿用配置的 149
  try {
    const b5 = w.document.querySelector('input[name=band][value="1"]');
    b5.checked = true;
    b5.dispatchEvent(new w.Event('change', { bubbles: true }));
    const ch = w.document.getElementById('channel');
    console.log('band 5.8G OK, channel =', ch.value, 'disabled =', ch.disabled,
      'range =', ch.min + '-' + ch.max);
    if (ch.disabled || ch.value !== '149' || ch.min !== '149' || ch.max !== '165') {
      console.log('BAND UI FAIL');
    }

    // 改成 153 应写进 channel_5g，同时 2.4G 的 channel 保持原值不被污染
    ch.value = '153';
    await w.save();
    const p5 = w._lastPost || {};
    console.log('5.8G save -> channel_5g =', p5.channel_5g, ', channel =', p5.channel,
      '(expect 153 / 6)');
    if (p5.channel_5g !== 153 || String(p5.channel) !== '6') console.log('CHANNEL 5G SAVE FAIL');

    // 切回 2.4G：输入框恢复 2.4G 旧值，范围回到 1-13
    const b2 = w.document.querySelector('input[name=band][value="0"]');
    b2.checked = true;
    b2.dispatchEvent(new w.Event('change', { bubbles: true }));
    console.log('band 2.4G back, channel =', ch.value, ', label =',
      JSON.stringify(w.document.getElementById('chlbl').textContent));
    if (ch.value !== '6' || ch.min !== '1' || ch.max !== '13') console.log('BAND 2G RESTORE FAIL');
  } catch (e) { console.log('band switch THREW:', e.message); }

  // 拨回发射：飞行参数/地图卡片应恢复显示
  try {
    const tx = w.document.querySelector('input[name=role][value=tx]');
    tx.checked = true;
    tx.dispatchEvent(new w.Event('change', { bubbles: true }));
    const disp = w.document.querySelector('.card.txonly').style.display;
    console.log('tx role OK, tx cards display:', JSON.stringify(disp), '(expect "")');
    if (disp !== '') console.log('ROLE SWITCH BACK FAIL');
    await w.save();
    console.log('save under tx role OK');
    const p = w._lastPost || {};
    console.log('save payload alt fields:', p.ground_altitude, p.altitude_wave,
      (p.ground_altitude !== undefined && p.altitude_wave !== undefined) ? 'OK' : 'FAIL');
    const keys = ['baro_altitude','heading','heading_mode','speed_vertical','vspd_mode',
                  'height_type','operator_location_type'];
    const miss = keys.filter(k => p[k] === undefined || p[k] === null);
    console.log('save payload new params:', keys.map(k => k + '=' + p[k]).join(' '),
      miss.length ? 'FAIL missing ' + miss : 'OK');
  } catch (e) { console.log('tx role THREW:', e.message); }

  // 热点密码：回读 → 保存上报 → 改密码时给出「用新密码重连」提示
  try {
    const d = w.document;
    const pw = d.getElementById('ap_password');
    console.log('ap_password prefilled:', JSON.stringify(pw.value), '(expect sd1471123)');
    if (pw.value !== 'sd1471123') console.log('AP PASS LOAD FAIL');
    pw.value = 'my-secret-2026';
    await w.save();
    const pp = w._lastPost || {};
    const m = d.getElementById('msg').textContent;
    console.log('ap_password posted:', JSON.stringify(pp.ap_password));
    console.log('save msg:', m);
    if (pp.ap_password !== 'my-secret-2026') console.log('AP PASS POST FAIL');
    if (!/热点密码已修改/.test(m)) console.log('AP PASS HINT FAIL');
    // 留空 = 开放热点：仍应原样上报空串（由后端决定接受）
    pw.value = '';
    await w.save();
    console.log('ap_password cleared ->', JSON.stringify((w._lastPost || {}).ap_password),
      '(expect "")');
    if ((w._lastPost || {}).ap_password !== '') console.log('AP PASS OPEN FAIL');
  } catch (e) { console.log('ap password THREW:', e.message); }

  // 巡游实况换算：填 15.166 m/s 时，报文应按用户值上报，且提示轨迹真实线速度
  try {
    const f = (k) => w.document.getElementById(k);
    f('base_latitude').value = '22.54';
    f('patrol_radius_lat').value = '0.00005';
    f('patrol_radius_lon').value = '0.00004';
    f('patrol_speed').value = '0.2';
    f('speed_horizontal').value = '15.166';
    f('patrol_speed').dispatchEvent(new w.Event('input', { bubbles: true }));
    const txt = f('patrolCalc').textContent;
    console.log('patrol calc:', txt);
    if (!txt.includes('0.14') || !txt.includes('15.2')) console.log('PATROL CALC FAIL');
  } catch (e) { console.log('patrol calc THREW:', e.message); }

  // 高度模型：相对高度 = 海拔 − 地面海拔（三联动）+ 波动幅度提示
  try {
    const f = (k) => w.document.getElementById(k);
    f('base_altitude_msl').value = '120';
    f('ground_altitude').value = '20';
    f('base_altitude_msl').dispatchEvent(new w.Event('input', { bubbles: true }));
    const agl = parseFloat(f('altitude_agl').value);
    console.log('alt link M=120 G=20 -> AGL =', f('altitude_agl').value, '(expect 100)',
      agl === 100 ? 'OK' : 'FAIL');
    f('altitude_agl').value = '50';
    f('altitude_agl').dispatchEvent(new w.Event('input', { bubbles: true }));
    const msl = parseFloat(f('base_altitude_msl').value);
    console.log('alt link AGL=50 -> MSL =', f('base_altitude_msl').value, '(expect 70)',
      msl === 70 ? 'OK' : 'FAIL');
    f('altitude_wave').value = '0';
    f('altitude_wave').dispatchEvent(new w.Event('input', { bubbles: true }));
    const t0 = f('patrolCalc').textContent;
    console.log('wave 0:', t0.includes('高度固定') && t0.includes('垂直速度恒为 0') ? 'OK' : 'FAIL');
    f('altitude_wave').value = '5';
    f('altitude_wave').dispatchEvent(new w.Event('input', { bubbles: true }));
    const t5 = f('patrolCalc').textContent;
    console.log('wave 5:', t5.includes('±5.0 m') && t5.includes('0.05') ? 'OK' : 'FAIL');
  } catch (e) { console.log('alt model THREW:', e.message); }

  // 气压高度默认跟随海拔；单独声明过就不再被海拔改动覆盖
  try {
    const f = (k) => w.document.getElementById(k);
    f('base_altitude_msl').value = '120';
    f('ground_altitude').value = '20';
    f('base_altitude_msl').dispatchEvent(new w.Event('input', { bubbles: true }));
    console.log('baro follow MSL:', f('baro_altitude').value, '(expect 120)',
      parseFloat(f('baro_altitude').value) === 120 ? 'OK' : 'FAIL');
    f('baro_altitude').value = '200';                       // 用户单独声明气压高度
    f('base_altitude_msl').value = '300';
    f('base_altitude_msl').dispatchEvent(new w.Event('input', { bubbles: true }));
    console.log('baro kept after MSL change:', f('baro_altitude').value, '(expect 200)',
      parseFloat(f('baro_altitude').value) === 200 ? 'OK' : 'FAIL');
  } catch (e) { console.log('baro follow THREW:', e.message); }

  // 航向 / 垂直速度「固定」模式：巡游提示改写，且不再按轨迹/波动推算
  try {
    const f = (k) => w.document.getElementById(k);
    f('speed_vertical').value = '2.5';
    f('vspd_mode').value = '1';
    f('vspd_mode').dispatchEvent(new w.Event('input', { bubbles: true }));
    const tv = f('patrolCalc').textContent;
    console.log('vspd fixed:', tv.includes('垂直速度固定为 2.5 m/s') &&
      tv.includes('不随高度波动变化') ? 'OK' : 'FAIL');
    f('heading').value = '270';
    f('heading_mode').value = '1';
    f('heading_mode').dispatchEvent(new w.Event('input', { bubbles: true }));
    const th = f('patrolCalc').textContent;
    console.log('heading fixed:', th.includes('航向固定为 270.0°') &&
      th.includes('不随轨迹切线变化') ? 'OK' : 'FAIL');
  } catch (e) { console.log('mode hint THREW:', e.message); }

  // 只改数值、不动来源下拉：应自动切到「固定」，否则巡游下个 10 秒步进就把它覆盖回去
  // （旧版水平速度被巡游覆写是同一个坑，这里守住不再复发）
  try {
    const f = (k) => w.document.getElementById(k);
    f('heading_mode').value = '0';
    f('vspd_mode').value = '0';
    f('heading').value = '123';
    f('heading').dispatchEvent(new w.Event('input', { bubbles: true }));
    f('speed_vertical').value = '-1.5';
    f('speed_vertical').dispatchEvent(new w.Event('input', { bubbles: true }));
    console.log('auto-switch heading_mode:', f('heading_mode').value,
      f('heading_mode').value === '1' ? 'OK' : 'FAIL');
    console.log('auto-switch vspd_mode:', f('vspd_mode').value,
      f('vspd_mode').value === '1' ? 'OK' : 'FAIL');
    const t = f('patrolCalc').textContent;
    console.log('auto-switch hint:', t.includes('航向固定为 123.0°') &&
      t.includes('垂直速度固定为 -1.5 m/s') ? 'OK' : 'FAIL');
    // 反过来：下拉仍可手动切回自动，此时数值不再被当作固定值
    f('heading_mode').value = '0';
    f('heading_mode').dispatchEvent(new w.Event('input', { bubbles: true }));
    console.log('auto-switch reversible:',
      f('patrolCalc').textContent.includes('航向由轨迹切线自动推算') ? 'OK' : 'FAIL');
  } catch (e) { console.log('auto-switch THREW:', e.message); }

  // 四个来源/基准下拉走自定义面板：点按钮展开、点条目选中并收起、隐藏字段同步
  try {
    const d = w.document;
    const btn = d.getElementById('dd5btn');
    const dd = d.getElementById('dd5');
    btn.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
    console.log('dd open:', dd.classList.contains('open') ? 'OK' : 'FAIL');
    const items = d.querySelectorAll('#dd5list .dd-item');
    console.log('dd item count:', items.length, '(expect 2)');
    items[1].dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
    console.log('dd pick -> heading_mode =', d.getElementById('heading_mode').value,
      '| text =', JSON.stringify(d.getElementById('dd5txt').textContent),
      '| closed =', !dd.classList.contains('open'));
    if (d.getElementById('heading_mode').value !== '1'
      || d.getElementById('dd5txt').textContent !== '固定（用左侧值）'
      || dd.classList.contains('open')) console.log('DD PICK FAIL');

    // 其余三个也应完成构造（条目数分别 2 / 2 / 3）
    const cnt = (n) => d.querySelectorAll('#dd' + n + 'list .dd-item').length;
    console.log('dd 6/7/8 items:', cnt(6), cnt(7), cnt(8), '(expect 2 2 3)');
    if (cnt(6) !== 2 || cnt(7) !== 2 || cnt(8) !== 3) console.log('DD BUILD FAIL');
  } catch (e) { console.log('dd ui THREW:', e.message); }

  // 固件升级（OTA）：信息区回填、未选文件不发请求、上传带上令牌与裸流头
  try {
    const d = w.document;
    await sleep(120);
    console.log('ota info:', d.getElementById('otaver').textContent,
      '|', d.getElementById('otapart').textContent,
      '|', d.getElementById('otacan').textContent);
    if (d.getElementById('otaver').textContent !== '2.0.0') console.log('OTA INFO FAIL');
    // 顶部横幅：项目名带 <wbr> 断点，入口按钮第二行显示同一版本号
    console.log('hdr name:', (d.querySelector('.hdr-nm') || {}).textContent,
      '| 入口版本:', (d.getElementById('otavh') || {}).textContent);
    if ((d.getElementById('otavh') || {}).textContent !== 'v2.0.0') console.log('HDR VER FAIL');

    // 未选文件：只提示，不许发请求
    w.otaStart();
    console.log('ota no-file msg:', d.getElementById('otatxt').textContent);
    if (w._otaReq) console.log('OTA NOFILE FAIL (request sent without file)');

    // 选一个 120KB 的假固件 -> 应 POST 裸流到 /api/ota/upload 并带令牌
    const fi = d.getElementById('otafile');
    const file = new w.File([new Uint8Array(120 * 1024)], 'remoteid_scanner.bin',
      { type: 'application/octet-stream' });
    Object.defineProperty(fi, 'files', { value: [file], configurable: true });
    w.otaStart();
    await sleep(150);
    const r = w._otaReq || {};
    console.log('ota req:', r.method, r.url, JSON.stringify(r.headers),
      'bytes =', r.body && r.body.size);
    if (r.method !== 'POST' || r.url !== '/api/ota/upload'
      || r.headers['X-OTA-Token'] !== 'tk123456'
      || r.headers['Content-Type'] !== 'application/octet-stream') console.log('OTA REQ FAIL');
    console.log('ota progress width:', JSON.stringify(d.getElementById('otafill').style.width),
      '| text:', d.getElementById('otatxt').textContent,
      '| btn disabled:', d.getElementById('otabtn').disabled);
    if (!/正在重启/.test(d.getElementById('otatxt').textContent)) console.log('OTA SUCCESS TEXT FAIL');
  } catch (e) { console.log('ota ui THREW:', e.message); }

  // 历史轨迹：只在接收模式显示；发射模式下整卡隐藏且不再轮询
  console.log('=== 历史轨迹 ===');
  const ld = w.document;
  const lgbox = ld.getElementById('logbox');
  console.log('  logbox 存在:', !!lgbox);
  w.setRole(true); w.modeUI();
  console.log('  发射模式隐藏:', lgbox.style.display === 'none');
  await w.logRefresh();     // 发射模式应直接跳过，不得把隐藏态改回可见
  console.log('  发射模式跳过刷新:', lgbox.style.display === 'none');
  w.setRole(false); w.modeUI();
  await w.logRefresh();
  console.log('  接收模式显示:', lgbox.style.display === '');
  console.log('  目标行数:', ld.querySelectorAll('#lgtargets .lgrow').length);
  console.log('  占用文案:', (ld.getElementById('lgused') || {}).textContent);
  console.log('  进度条宽度:', (ld.getElementById('lgfill') || {}).style
    ? ld.getElementById('lgfill').style.width : '');

  // 打开某个目标的轨迹：分页 + 航线回放 + 可收起
  console.log('  --- 详情 / 分页 / 回放 ---');
  ld.querySelector('button[data-lg]').click();
  await new Promise((r) => setTimeout(r, 40));
  console.log('  详情展开:', ld.getElementById('lgdet').style.display === '');
  console.log('  点位行数:', ld.querySelectorAll('#lgpoints .lgpt[data-i]').length);
  console.log('  页码按钮数:', ld.querySelectorAll('#lgpage button[data-pg]').length);
  console.log('  分页文案:', (ld.querySelector('#lgpage .info') || {}).textContent);
  console.log('  首页请求带 offset=0:', /[?&]offset=0/.test(w._logReq || ''));
  ld.querySelector('#lgpage button[data-pg="1"]').click();
  await new Promise((r) => setTimeout(r, 40));
  console.log('  第2页请求:', /offset=30/.test(w._logReq || ''));
  console.log('  第2页高亮页码:', (ld.querySelector('#lgpage button.on') || {}).textContent);
  w.lgPlayToggle();
  await new Promise((r) => setTimeout(r, 480));
  console.log('  回放按钮转为暂停:', ld.getElementById('lgpp').textContent === '暂停');
  console.log('  回放行高亮:', ld.querySelectorAll('#lgpoints .lgpt.cur').length);
  w.lgReset();
  console.log('  重置后按钮回播放:', ld.getElementById('lgpp').textContent === '播放',
    '| 高亮行', ld.querySelectorAll('#lgpoints .lgpt.cur').length);
  // 再点同一个目标应直接收起（原来打开后收不回去）
  ld.querySelector('button[data-lg]').click();
  await new Promise((r) => setTimeout(r, 40));
  console.log('  再点同目标收起:', ld.getElementById('lgdet').style.display === 'none');
  // 卡片标题折叠 / 展开
  w.logFold();
  console.log('  卡片折叠:', ld.getElementById('lgbody').style.display === 'none',
    '| 标签', ld.getElementById('lgtog').textContent);
  w.logFold();
  console.log('  卡片展开:', ld.getElementById('lgbody').style.display === '',
    '| 标签', ld.getElementById('lgtog').textContent);

  console.log('=== 运行后错误 ===');
  console.log(w._pageErrors.length ? w._pageErrors : '(none)');
  console.log('DONE');

  // 页面里有常驻轮询/授时定时器，显式关闭窗口并退出，避免 node 事件循环挂住
  const failed = w._pageErrors.length > 0;
  try { dom.window.close(); } catch (e) {}
  process.exit(failed ? 1 : 0);
})();
