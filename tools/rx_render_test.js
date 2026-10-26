// 渲染冒烟测试：桩 DOM + 假 /api/rx 数据，跑通 pollRx 全流程（卡片列表 + 飞手位置）
const fs = require('fs');
const js = fs.readFileSync(__dirname + '/_page.js', 'utf8');

const start = js.indexOf('const RXST=');
const endMarker = "catch(e){q('rxstate').textContent='轮询失败：'+e;}}";
const end = js.indexOf(endMarker);
const rx = js.slice(start, end + endMarker.length);
if (rx.length < 100) { console.error('SLICE FAILED'); process.exit(1); }

const ids = ['s_online','s_rid','s_total','s_ok','s_ovf','s_ovfbox','rxstate','rxlist'];
const store = {};
function mkNode(tag){
  return {
    tag, children: [], _html: '', className: '', textContent: '', dataset: {}, id: tag,
    style: {}, classList: {
      _s: new Set(),
      add(c){ this._s.add(c); }, remove(c){ this._s.delete(c); },
      toggle(c, f){ (f === undefined ? !this._s.has(c) : f) ? this._s.add(c) : this._s.delete(c); },
      contains(c){ return this._s.has(c); },
    },
    set innerHTML(v){ this._html = v; this.children = []; },
    get innerHTML(){ return this._html; },
    appendChild(c){ this.children.push(c); },
    insertAdjacentHTML(pos, h){ this._html += h; },
    before(n){},
    querySelectorAll(sel){
      return this.children.filter(c => c.dataset && c.dataset.mac)
        .map(c => ({ dataset: c.dataset, classList: c.classList }));
    },
    querySelector(sel){ return null; },
    scrollIntoView(){}, onclick: null, getBoundingClientRect(){ return {width:320}; },
  };
}
ids.forEach(i => store[i] = mkNode('div'));
global.document = {
  getElementById: id => store[id] || null,
  createElement: mkNode,
};
global.Date = class { constructor(){} toLocaleTimeString(){ return '12:00:00'; } };
global.window = {};

// ---- Leaflet 桩 ----
let markers = [];
function mkLayer(extra){ const m = Object.assign({
  _ll: null, added: 0, removed: 0,
  addTo(){ this.added++; markers.push(this); return this; },
  setLatLng(ll){ this._ll = ll; return this; },
  getLatLng(){ return this._ll; },
  setStyle(){}, setLatLngs(){}, removeLayer(x){ if (x) x.removed++; },
  bindPopup(){ return this; }, openPopup(){ return this; },
  on(){ return this; }, setView(){ return this; },
  fitBounds(){ this._fitted = true; return this; }, getZoom(){ return 13; },
}, extra || {}); return m; }
global.L = {
  map(){ return mkLayer({}); },
  tileLayer(){ return mkLayer(); },
  circleMarker(ll, o){ return mkLayer({ _ll: ll, _o: o }); },
  marker(ll, o){ return mkLayer({ _ll: ll, _o: o }); },
  polyline(ll, o){ return mkLayer({ _lls: ll, _o: o }); },
  circle(ll, o){ return mkLayer({ _ll: ll, _o: o }); },
  latLngBounds(pts){ return { pad(){ return pts[0]; } }; },
  divIcon(){ return {}; },
};


const FAKE = {
  stats: { total: 5321, rid: 412, ok: 409, ovf: 0 },
  tracks: [
    { mac:'AA:BB:CC:DD:EE:01', rssi:-42, channel:6, protocol:4,
      uas_id:'1581F5QJ8ABCDEF0123', drone_name:'', operator_id:'',
      realname_id:'CN8823AB', ua_type:2,
      latitude:22.543218, longitude:114.057321, altitude_geo:88.4,
      speed_horizontal:12.3, speed_vertical:-0.8, direction:187,
      status:2, msg_count:57, age_ms:800,
      has_op_loc:true, op_latitude:22.530001, op_longitude:114.040002, op_altitude_geo:25 },
    { mac:'AA:BB:CC:DD:EE:02', rssi:-71, channel:6, protocol:3,
      uas_id:'1581F8LQ9XYZ9998887', drone_name:'Mavic 4 Pro', operator_id:'OP1234567',
      realname_id:'', ua_type:2,
      latitude:22.540001, longitude:114.060002, altitude_geo:45.0,
      speed_horizontal:0.0, speed_vertical:0.0, direction:0,
      status:1, msg_count:12, age_ms:4200,
      has_op_loc:false, op_latitude:0, op_longitude:0, op_altitude_geo:0 },
  ],
};
global.fetch = async () => ({ json: async () => FAKE });

const fn = new Function(rx + '\nreturn {pollRx, rxCard};');
const api = fn();
const A = (x) => console.log(x);
// 卡片数 = rxlist 直接子节点里带 dataset.mac 的（分区标题不带 mac）
const cardCount = () => store.rxlist.children.filter(c => c.dataset && c.dataset.mac).length;
api.pollRx().then(() => {
  A('cards          : ' + cardCount() + ' (expect 2)');
  A('sections       : ' + (store.rxlist.children[0].textContent.indexOf('在线信号 · 2') >= 0 ? 'OK' : 'FAIL')
    + ' — ' + store.rxlist.children[0].textContent);
  const c1 = store.rxlist.children[1];
  A('op pos on card1: ' + (c1._html.includes('飞手位置') && c1._html.includes('22.530001') ? 'OK' : 'FAIL'));
  A('no op on card2 : ' + (!store.rxlist.children[2]._html.includes('飞手位置') ? 'OK' : 'FAIL'));
  A('online no gray : ' + (!store.rxlist.children[1].className.includes('off') ? 'OK' : 'FAIL'));
  // ---- 目标 2 转为离线：应有「已离线」分区 + 灰度卡片 ----
  FAKE.tracks[1].active = false;
  return api.pollRx().then(() => {
    const secs = store.rxlist.children.filter(c => !c.dataset || !c.dataset.mac)
      .map(c => c.textContent).join(' | ');
    A('split sections : ' + (secs.indexOf('在线信号 · 1') >= 0 && secs.indexOf('已离线 · 1') >= 0 ? 'OK' : 'FAIL') + ' — ' + secs);
    const offCard = store.rxlist.children.filter(c => c.dataset && c.dataset.mac && c.className.includes('off'));
    A('offline gray   : ' + (offCard.length === 1 ? 'OK' : 'FAIL ' + offCard.length));
    // ---- 目标离线移除：只剩 1 架 ----
    FAKE.tracks = FAKE.tracks.slice(0, 1);
    return api.pollRx().then(() => {
      A('offline remove : ' + (cardCount() === 1 ? 'OK' : 'FAIL ' + cardCount()));
      // ---- 空列表 ----
      FAKE.tracks = [];
      return api.pollRx();
    });
  });
}).then(() => {
  A('empty state    : ' + (store.rxlist._html.includes('尚未发现') ? 'OK' : 'FAIL'));
  console.log('\nALL SMOKE TESTS DONE');
});
