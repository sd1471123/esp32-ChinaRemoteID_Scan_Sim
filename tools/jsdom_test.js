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
      if (u === '/api/config' && opt && opt.method === 'POST')
        return { json: async () => ({ ok: true }) };   // 走进 save() 成功分支
      if (u === '/api/config') return { json: async () => ({
        device_mode: 0, rid_standard: 0, uas_id: 'X', drone_name: 'X',
        channel: '6', beacon_interval_ms: 5, ua_type: '2',
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
      return { json: async () => ({}) };
    };
    window.confirm = () => true;
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

  // 模拟：切到 5.8G 频段（单射频互斥，信道应自动锁到 149 并禁用输入）
  try {
    const b5 = w.document.querySelector('input[name=band][value="1"]');
    b5.checked = true;
    b5.dispatchEvent(new w.Event('change', { bubbles: true }));
    const ch = w.document.getElementById('channel');
    console.log('band 5.8G OK, channel =', ch.value, 'disabled =', ch.disabled);
    if (ch.value !== '149' || !ch.disabled) console.log('BAND UI FAIL');
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
  } catch (e) { console.log('tx role THREW:', e.message); }

  console.log('=== 运行后错误 ===');
  console.log(w._pageErrors.length ? w._pageErrors : '(none)');
  console.log('DONE');

  // 页面里有常驻轮询/授时定时器，显式关闭窗口并退出，避免 node 事件循环挂住
  const failed = w._pageErrors.length > 0;
  try { dom.window.close(); } catch (e) {}
  process.exit(failed ? 1 : 0);
})();
