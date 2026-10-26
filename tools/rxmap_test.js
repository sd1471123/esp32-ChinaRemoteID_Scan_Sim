// 接收端地图模块（main/rxmap_page.js）隔离性与行为测试
// 重点验证：1) 不污染任何全局（与发射端地图零冲突）2) 建图/更新/清理/聚焦 3) 失败降级
const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../main/rxmap_page.js', 'utf8');

// ---- 最小 DOM 桩 ----
function mkNode(tag) {
  return {
    tag, children: [], _html: '', textContent: '', id: tag || '',
    style: {}, dataset: {}, parentNode: null,
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; },
    insertBefore(n) { this.children.unshift(n); n.parentNode = this; return n; },
    removeChild(n) { const i = this.children.indexOf(n); if (i >= 0) this.children.splice(i, 1); },
    set innerHTML(v) { this._html = v; this.children = []; },
    get innerHTML() { return this._html; },
  };
}
const store = {};
['rxmapbox', 'rxmap', 'rxfit', 'rxbox', 'rxlist', 'rxmapfail', 'rxgridnote', 'rxgrid']
  .forEach(id => store[id] = mkNode('div'));
// 离线网格用的 canvas 上下文桩
const drawn = { arcs: 0, lines: 0, texts: [], dashed: 0 };
function mkCtx() {
  return {
    canvas: store.rxgrid, fillStyle: '', strokeStyle: '', lineWidth: 1, font: '',
    fillRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {},
    arc() { drawn.arcs++; }, fill() {}, save() {}, restore() {}, translate() {}, rotate() {},
    strokeRect() {}, setLineDash() { drawn.dashed++; },
    fillText(t) { drawn.texts.push(t); },
  };
}
store.rxgrid.getContext = () => mkCtx();
store.rxgrid.style = {};
global.document = {
  getElementById: id => store[id] || null,
  createElement: mkNode,
};
global.window = {};

// ---- Leaflet 桩：记录所有图层操作 ----
const layers = { added: [], removed: [], fit: 0, setView: 0, invalidate: 0, opened: 0 };
function mkLayer(ll, o) {
  return {
    _ll: ll, _o: o, _popup: null,
    addTo() { layers.added.push(this); return this; },
    setLatLng(v) { this._ll = v; return this; },
    getLatLng() { return this._ll; },
    setLatLngs(v) { this._lls = v; return this; },
    setStyle() { return this; },
    setPopupContent(h) { this._popup = h; return this; },
    bindPopup(h) { this._popup = h; return this; },
    openPopup() { layers.opened++; return this; },
    on() { return this; },
    setView(v) { this._ll = v; layers.setView++; return this; },
    fitBounds() { layers.fit++; return this; },
    invalidateSize() { layers.invalidate++; return this; },
    getZoom() { return 13; },
    removeLayer(x) { if (x) layers.removed.push(x); return this; },
  };
}
global.L = {
  map(id) { layers.mapId = id; return mkLayer([22.5, 114.05]); },
  tileLayer() { return mkLayer(); },
  circleMarker(ll, o) { return mkLayer(ll, o); },
  circle(ll, o) { return mkLayer(ll, o); },
  polyline(ll, o) { return mkLayer(ll, o); },
  divIcon(o) { return { _icon: o }; },
  latLngBounds(pts) { return { pad() { return pts; } }; },
};

// 浏览器里 Leaflet 挂在 window 上，桩环境对齐
global.window.L = global.L;

// ---- 加载模块 ----
new Function(src)();
const M = global.window.RXMAP;
const A = x => console.log(x);

A('exports RXMAP  : ' + (M && typeof M.boot === 'function' && typeof M.update === 'function'
  && typeof M.resize === 'function' && typeof M.focus === 'function' ? 'OK' : 'FAIL'));
A('no global leak : ' + (['map', 'opMarker', 'dMarker', 'circle', 'ginit', 'loadLeaflet', 'dotIcon']
  .every(k => global.window[k] === undefined) ? 'OK' : 'FAIL ' +
  JSON.stringify(Object.keys(global.window))));

// ---- boot：容器应显示，地图建在 rxmap 上（不是 map）----
M.boot();
A('boot shows box: ' + (store.rxmapbox.style.display === 'block' ? 'OK' : 'FAIL'));
A('map on rxmap  : ' + (layers.mapId === 'rxmap' ? 'OK' : 'FAIL ' + layers.mapId));

// ---- update：两架无人机（一架带飞手位置）----
const T = [
  { mac: 'AA:01', protocol: 4, uas_id: 'U1', rssi: -40, latitude: 22.54, longitude: 114.05, altitude_geo: 80, has_op_loc: true, op_latitude: 22.53, op_longitude: 114.04 },
  { mac: 'AA:02', protocol: 3, uas_id: 'U2', rssi: -70, latitude: 22.56, longitude: 114.07, altitude_geo: 50, has_op_loc: false },
];
M.update(T);
const markers = layers.added.filter(l => l._o && l._o.radius);
A('drone+pilot mk: ' + (markers.length === 3 ? 'OK' : 'FAIL ' + markers.length) + ' (2 无人机 + 1 飞手)');
A('fitBounds call : ' + (layers.fit + layers.setView > 0 ? 'OK' : 'FAIL'));
A('popup has id   : ' + (((markers[0]._popup || '').includes('U1')) ? 'OK' : 'FAIL'));

// ---- 同一 mac 再次更新：不应重复建点 ----
const before = layers.added.length;
M.update(T);
A('no duplicate  : ' + (layers.added.length === before ? 'OK' : 'FAIL'));

// ---- 飞行航迹：位置变化后建 trail 线，之后更新只生长不重建 ----
let polyCreated = 0;
const origPoly = global.L.polyline;
global.L.polyline = function (ll, o) { polyCreated++; return origPoly(ll, o); };
T[0].latitude = 22.5401;
M.update(T);
A('trail created : ' + (polyCreated === 1 ? 'OK' : 'FAIL ' + polyCreated) + ' (移动一次后应建 1 条航迹线)');
const pc1 = polyCreated;
T[0].latitude = 22.5402;
M.update(T);
A('trail grows   : ' + (polyCreated === pc1 ? 'OK' : 'FAIL ' + polyCreated) + ' (后续移动只 setLatLngs 不重建)');

// ---- 目标离线：AA:02（无飞手位置）只移除 1 个无人机点 ----
let rm = layers.removed.length;
M.update([T[0]]);
A('offline remove : ' + (layers.removed.length - rm === 1 ? 'OK' : 'FAIL ' + (layers.removed.length - rm)));
// ---- 带飞手位置的目标离线：应移除无人机点 + 飞手点 + 连线 + 航迹线 共 4 个 ----
rm = layers.removed.length;
M.update([T[1]]);
A('offline 4 layer: ' + (layers.removed.length - rm === 4 ? 'OK' : 'FAIL ' + (layers.removed.length - rm)));
M.update(T);

// ---- focus / 再次点击取消 ----
M.focus('AA:01');
A('focus zoom    : ' + (layers.setView > 0 && layers.opened > 0 ? 'OK' : 'FAIL'));
M.focus('AA:01');
A('focus toggle  : ' + (layers.removed.filter(l => l._o && l._o.radius === 55).length >= 1 ? 'OK' : 'FAIL'));

// ---- resize 不应抛错 ----
try { M.resize(); A('resize safe   : OK'); } catch (e) { A('resize safe   : FAIL ' + e.message); }

// ---- 空列表：清空所有图层 ----
const rmBefore = layers.removed.length;
M.update([]);
A('empty clear   : ' + (layers.removed.length > rmBefore ? 'OK' : 'FAIL'));

// ---- 无 Leaflet（热点离线）：应切到离线网格，而不是直接放弃 ----
delete global.window.L;
delete global.window.RXMAP;
drawn.arcs = 0; drawn.texts = [];
new Function(src)();
global.window.RXMAP.boot();
A('offline grid  : ' + (store.rxgrid.style.display === 'block' && store.rxmap.style.display === 'none' ? 'OK' : 'FAIL'));
A('grid note     : ' + (store.rxgridnote.innerHTML.indexOf('离线网格') >= 0 ? 'OK' : 'FAIL'));
global.window.RXMAP.update(T);
A('grid plotted  : ' + (drawn.arcs >= 2 ? 'OK' : 'FAIL ' + drawn.arcs) + ' (无人机点 + 中心点)');
A('grid scale    : ' + (drawn.texts.some(t => /米\/像素/.test(t)) ? 'OK' : 'FAIL') + ' — ' + (drawn.texts.join(' | ') || '(无)'));
A('grid dashed   : ' + (drawn.dashed > 0 ? 'OK' : 'FAIL') + ' (飞手连线虚线)');
// 空目标时网格应提示暂无位置
drawn.texts = [];
global.window.RXMAP.update([]);
A('grid empty    : ' + (drawn.texts.some(t => t.indexOf('暂无目标位置') >= 0) ? 'OK' : 'FAIL'));
// 网格模式下 resize / focus 不应抛错
try { global.window.RXMAP.resize(); global.window.RXMAP.focus('AA:01'); A('grid safe    : OK'); }
catch (e) { A('grid safe    : FAIL ' + e.message); }
A('no fail note  : ' + (store.rxmapfail.textContent === '' ? 'OK' : 'FAIL — 误报降级: ' + store.rxmapfail.textContent));
console.log('\nRXMAP TESTS DONE');
