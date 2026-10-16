/* 接收端信号地图 —— 独立资源 /rxmap.js，按需加载
 *
 * 隔离原则（与发射端地图互不干扰的关键）：
 *   1. 只向 window 暴露一个对象 RXMAP（boot/update/resize/focus），不新增其它全局变量；
 *   2. 全部状态收在闭包内（m / lay / autoFit），绝不使用 map、opMarker、dMarker、
 *      circle、ginit、dotIcon、drawCircle 等发射端地图的变量与函数；
 *   3. 容器 id 为 rxmap / rxmapbox，与发射端的 map 完全不同；
 *   4. 不依赖页面的 q()/fld()/esc()，自带工具函数，页面出问题也不会牵连这里；
 *   5. 全部 ES5 语法 + 字符串拼接（不用模板串），最大化兼容性；
 *   6. 任何异常都在内部捕获，失败只降级为纯列表，绝不影响配置页与接收列表。
 */
(function () {
  var LB = null;           // Leaflet 引用：boot 时从 window.L 取，避免声明同名变量遮蔽全局
  var m = null;            // 本模块自己的地图实例
  var lay = {};            // mac -> { d:无人机点, o:飞手点, line:连线, ring:焦点环 }
  var autoFit = true;      // 是否自动缩放（用户手动拖动后置 false）
  var focusMac = null;     // 当前聚焦目标

  function q(id) { return document.getElementById(id); }

  function esc(s) {
    return String(s === null || s === undefined ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function f6(v) { return (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(6); }
  function f1(v) { return (v === null || v === undefined || isNaN(v)) ? '--' : Number(v).toFixed(1); }
  function protoName(p) { return p === 4 ? 'GB46750' : (p === 3 ? 'GB42590' : 'ASTM'); }
  function protoColor(p) { return p === 4 ? '#5b21b6' : (p === 3 ? '#0369a1' : '#6b7280'); }
  function noPos(t) { return !t || t.latitude === 0 && t.longitude === 0; }
  function noOp(t) { return !t || !t.has_op_loc || t.op_latitude === 0 && t.op_longitude === 0; }

  // 飞手位置用菱形图标，用 DOM 构造避免任何 HTML 字符串拼接
  function planeIcon() {
    var d = document.createElement('div');
    d.style.cssText = 'width:12px;height:12px;background:#e67e22;border:2px solid #fff;'
      + 'border-radius:3px;box-shadow:0 1px 3px rgba(0,0,0,.4);transform:rotate(45deg)';
    return LB.divIcon({ className: '', html: d, iconSize: [12, 12], iconAnchor: [6, 6] });
  }

  function popHtml(t, drone) {
    if (drone) {
      return '<b>' + esc(t.uas_id || t.mac) + '</b><br>' + protoName(t.protocol)
        + ' · ' + esc(t.drone_name || '') + '<br>' + f6(t.latitude) + ', ' + f6(t.longitude)
        + '<br>高度 ' + f1(t.altitude_geo) + ' m · RSSI ' + t.rssi + ' dBm';
    }
    return '<b>飞手位置 (遥控站)</b><br>' + esc(t.uas_id || t.mac) + '<br>'
      + f6(t.op_latitude) + ', ' + f6(t.op_longitude);
  }

  // 降级提示：仅在连 canvas 网格都不行时才隐藏地图容器并给一行说明
  function fail(msg) {
    try {
      mode = 'none';
      var box = q('rxmapbox');
      if (box) box.style.display = 'none';
      var n = q('rxmapfail');
      if (!n) {
        n = document.createElement('div');
        n.className = 'note';
        n.id = 'rxmapfail';
        var host = q('rxbox');
        if (host && q('rxlist')) host.insertBefore(n, q('rxlist'));
      }
      n.textContent = msg || '地图不可用，已降级为纯列表模式';
    } catch (e) { /* 降级过程自身出错则静默 */ }
  }

  // ---- 离线网格底图 ----
  // 设备热点没有互联网时 Leaflet/瓦片都取不到，此时改用 canvas 手绘网格：
  // 与发射端同款（米/像素比例尺 + 网格线），但范围自动贴合当前所有目标，
  // 因此即使完全离线也能看出无人机与飞手的相对方位和距离。
  var mode = 'none';      // map | grid | none
  var mpp = 20;           // 米/像素，会按目标范围自动收缩

  function gridCtx() {
    var cv = q('rxgrid');
    if (!cv || !cv.getContext) return null;
    return cv.getContext('2d');
  }

  // 依据所有目标经纬度计算网格中心与米/像素，使全部目标落在画布内
  function gridScale(tracks) {
    var i, t, n = 0, minLa = 1e9, maxLa = -1e9, minLo = 1e9, maxLo = -1e9;
    for (i = 0; i < tracks.length; i++) {
      t = tracks[i];
      var pts = [[t.latitude, t.longitude]];
      if (!noOp(t)) pts.push([t.op_latitude, t.op_longitude]);
      for (var j = 0; j < pts.length; j++) {
        var la = pts[j][0], lo = pts[j][1];
        if (la === 0 && lo === 0) continue;
        if (la < minLa) minLa = la;
        if (la > maxLa) maxLa = la;
        if (lo < minLo) minLo = lo;
        if (lo > maxLo) maxLo = lo;
        n++;
      }
    }
    if (!n) return null;
    var cLa = (minLa + maxLa) / 2, cLo = (minLo + maxLo) / 2;
    // 经度方向的实际米数随纬度收缩
    var spanX = (maxLo - minLo) * 111320 * Math.cos(cLa * Math.PI / 180);
    var spanY = (maxLa - minLa) * 111320;
    var need = Math.max(spanX, spanY, 60) * 1.25 / 320;   // 留 25% 余量
    // 归整到 1/2/5 的整数档位，比例尺读数更规整
    var steps = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000];
    var v = steps[steps.length - 1];
    for (i = 0; i < steps.length; i++) {
      if (steps[i] >= need) { v = steps[i]; break; }
    }
    return { lat: cLa, lon: cLo, mpp: v };
  }

  // 把经纬度投影到网格像素坐标
  function gridXY(s, la, lo) {
    var dx = (lo - s.lon) * 111320 * Math.cos(s.lat * Math.PI / 180);
    var dy = (la - s.lat) * 111320;
    return [160 + dx / s.mpp, 160 - dy / s.mpp];
  }

  function drawRxGrid(tracks) {
    var x = gridCtx();
    if (!x) { fail('离线网格不可用，已降级为纯列表模式'); return; }
    var s = gridScale(tracks);
    var box = q('rxmapbox');
    var cv = q('rxgrid');
    var mapDiv = q('rxmap');
    if (box) box.style.display = 'block';
    if (mapDiv) mapDiv.style.display = 'none';
    if (cv) cv.style.display = 'block';
    var note = q('rxgridnote');
    if (note) {
      note.innerHTML = '<b>离线网格</b>（设备热点无互联网，地图底图不可用）'
        + '　比例尺 ' + (s ? s.mpp : 20) + ' 米/像素　网格间距 40 像素';
    }
    // 底
    x.fillStyle = '#fafafa';
    x.fillRect(0, 0, 320, 320);
    x.strokeStyle = '#e2e2e2';
    x.lineWidth = 1;
    for (var i = 0; i <= 320; i += 40) {
      x.beginPath(); x.moveTo(i, 0); x.lineTo(i, 320); x.stroke();
      x.beginPath(); x.moveTo(0, i); x.lineTo(320, i); x.stroke();
    }
    if (!s) {
      x.fillStyle = '#9ca3af';
      x.font = '12px sans-serif';
      x.fillText('暂无目标位置', 110, 164);
      return;
    }
    // 无人机点
    for (i = 0; i < tracks.length; i++) {
      var t = tracks[i];
      if (noPos(t)) continue;
      var p = gridXY(s, t.latitude, t.longitude);
      x.beginPath();
      x.arc(p[0], p[1], 6, 0, 6.3);
      x.fillStyle = protoColor(t.protocol);
      x.fill();
      x.lineWidth = 2;
      x.strokeStyle = '#fff';
      x.stroke();
    }
    // 飞手点（橙色菱形）+ 连线
    for (i = 0; i < tracks.length; i++) {
      var t2 = tracks[i];
      if (noOp(t2)) continue;
      var o = gridXY(s, t2.op_latitude, t2.op_longitude);
      if (!noPos(t2)) {
        var d = gridXY(s, t2.latitude, t2.longitude);
        x.setLineDash([4, 3]);
        x.strokeStyle = '#e67e22';
        x.lineWidth = 1.5;
        x.beginPath(); x.moveTo(o[0], o[1]); x.lineTo(d[0], d[1]); x.stroke();
        x.setLineDash([]);
      }
      x.save();
      x.translate(o[0], o[1]);
      x.rotate(Math.PI / 4);
      x.fillStyle = '#e67e22';
      x.fillRect(-5, -5, 10, 10);
      x.lineWidth = 2;
      x.strokeStyle = '#fff';
      x.strokeRect(-5, -5, 10, 10);
      x.restore();
    }
    // 中心十字与比例尺文字
    x.strokeStyle = '#bbb';
    x.beginPath(); x.moveTo(160, 150); x.lineTo(160, 170); x.stroke();
    x.beginPath(); x.moveTo(150, 160); x.lineTo(170, 160); x.stroke();
    x.fillStyle = '#555';
    x.font = '11px sans-serif';
    x.fillText(s.mpp + ' 米/像素（网格间距 40 像素）', 8, 312);
    x.fillStyle = '#0a7d43';
    x.beginPath(); x.arc(160, 160, 4, 0, 6.3); x.fill();
    x.fillStyle = '#555';
    x.fillText('中心=网格原点', 170, 164);
  }

  function fitNow() {
    try {
      if (!m) return;
      var pts = [];
      for (var k in lay) {
        var l = lay[k];
        if (l.d) pts.push(l.d.getLatLng());
        if (l.o) pts.push(l.o.getLatLng());
      }
      if (!LB) return;
      if (pts.length > 1) m.fitBounds(LB.latLngBounds(pts).pad(0.25));
      else if (pts.length === 1) m.setView(pts[0], 15);
    } catch (e) { /* 忽略 */ }
  }

  // 进入接收模式、容器已可见后调用：创建地图（幂等）
  function boot() {
    try {
      // Leaflet 由页面 head 的 CDN 脚本提供。热点无互联网时取不到，
      // 此时不降级为纯列表，而是改用内置的离线网格底图继续展示位置
      var box = q('rxmapbox');
      if (!box) return;
      var l = window.L;
      if (!l || !l.map) {
        mode = 'grid';
        drawRxGrid(lastTracks || []);
        return;
      }
      LB = l;
      var note = q('rxmapfail');
      if (note && note.parentNode) note.parentNode.removeChild(note);
      box.style.display = 'block';
      if (!m) {
        m = l.map('rxmap').setView([22.5, 114.05], 13);
        l.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
          { maxZoom: 19, attribution: '&copy; OpenStreetMap' }).addTo(m);
        m.on('dragstart zoomstart', function () { autoFit = false; });
        var fb = q('rxfit');
        if (fb) fb.onclick = function () { autoFit = true; fitNow(); };
      }
      mode = 'map';
      var gd = q('rxgrid');
      if (gd) gd.style.display = 'none';
      var mp = q('rxmap');
      if (mp) mp.style.display = '';
      // 容器刚从 display:none 变为可见，必须刷新尺寸，否则瓦片错位/空白
      setTimeout(function () { try { m.invalidateSize(); } catch (e) {} }, 60);
    } catch (e) {
      fail('地图初始化失败：' + e);
    }
  }

  // 容器显示状态变化后调用
  function resize() {
    try {
      if (mode === 'grid') { drawRxGrid(lastTracks || []); return; }
      if (m) m.invalidateSize();
    } catch (e) {}
  }

  // 每 2 秒的轮询结果交给这里
  var lastTracks = [];
  function update(tracks) {
    lastTracks = tracks || [];
    if (mode === 'grid') { drawRxGrid(lastTracks); return; }
    if (mode !== 'map' || !m || !LB) {
      if (m) clearAll();
      return;
    }
    if (!tracks || !tracks.length) {
      clearAll();
      return;
    }
    try {
      var seen = {}, i, t, l, pts = [];
      for (i = 0; i < tracks.length; i++) {
        t = tracks[i];
        seen[t.mac] = 1;
        l = lay[t.mac];
        if (!l) l = lay[t.mac] = { d: null, o: null, line: null, ring: null };

        if (!noPos(t)) {
          if (!l.d) {
            l.d = LB.circleMarker([t.latitude, t.longitude], {
              radius: 7, color: '#fff', weight: 2,
              fillColor: protoColor(t.protocol), fillOpacity: 0.95
            }).addTo(m);
            l.d.bindPopup(popHtml(t, true));
          } else {
            l.d.setLatLng([t.latitude, t.longitude]);
            l.d.setStyle({ fillColor: protoColor(t.protocol) });
            l.d.setPopupContent(popHtml(t, true));
          }
          pts.push(l.d.getLatLng());
        } else if (l.d) {
          m.removeLayer(l.d); l.d = null;
        }

        if (!noOp(t)) {
          if (!l.o) {
            l.o = LB.circleMarker([t.op_latitude, t.op_longitude], {
              radius: 6, color: '#fff', weight: 2, fillColor: '#e67e22', fillOpacity: 0.95
            }).addTo(m);
            l.o.bindPopup(popHtml(t, false));
          } else {
            l.o.setLatLng([t.op_latitude, t.op_longitude]);
            l.o.setPopupContent(popHtml(t, false));
          }
          pts.push(l.o.getLatLng());
          if (!l.line) {
            l.line = LB.polyline([], { color: '#e67e22', weight: 1.5, dashArray: '5,5', opacity: 0.85 }).addTo(m);
          }
          l.line.setLatLngs([[t.op_latitude, t.op_longitude], [t.latitude, t.longitude]]);
        } else {
          if (l.o) { m.removeLayer(l.o); l.o = null; }
          if (l.line) { m.removeLayer(l.line); l.line = null; }
        }
      }
      // 清理已离线目标
      for (var k in lay) {
        if (seen[k]) continue;
        var x = lay[k];
        if (x.d) m.removeLayer(x.d);
        if (x.o) m.removeLayer(x.o);
        if (x.line) m.removeLayer(x.line);
        if (x.ring) m.removeLayer(x.ring);
        delete lay[k];
      }
      if (focusMac && lay[focusMac] && lay[focusMac].ring) {
        var ll = lay[focusMac].d ? lay[focusMac].d.getLatLng()
          : (lay[focusMac].o ? lay[focusMac].o.getLatLng() : null);
        if (ll) lay[focusMac].ring.setLatLng(ll);
        else { m.removeLayer(lay[focusMac].ring); lay[focusMac].ring = null; }
      }
      if (autoFit) fitNow();
    } catch (e) {
      fail('地图刷新失败：' + e);
    }
  }

  function clearAll() {
    try {
      for (var k in lay) {
        var x = lay[k];
        if (x.d) m.removeLayer(x.d);
        if (x.o) m.removeLayer(x.o);
        if (x.line) m.removeLayer(x.line);
        if (x.ring) m.removeLayer(x.ring);
        delete lay[k];
      }
      focusMac = null;
    } catch (e) {}
  }

  // 点击接收列表卡片时聚焦到地图
  function focus(mac) {
    try {
      if (mode !== 'map' || !m || !LB) return;
      if (focusMac === mac) {            // 再点一次取消
        var old = lay[focusMac];
        if (old && old.ring) { m.removeLayer(old.ring); old.ring = null; }
        focusMac = null;
        autoFit = true;
        return;
      }
      if (focusMac && lay[focusMac] && lay[focusMac].ring) {
        m.removeLayer(lay[focusMac].ring);
        lay[focusMac].ring = null;
      }
      focusMac = mac;
      autoFit = false;
      var l = lay[mac];
      if (!l) return;
      var t = l.d ? l.d.getLatLng() : (l.o ? l.o.getLatLng() : null);
      if (!t) return;
      l.ring = LB.circle(t, { radius: 55, color: '#0f8a4d', weight: 2, dashArray: '4,4', fill: false }).addTo(m);
      m.setView(t, Math.max(m.getZoom(), 16));
      if (l.d) l.d.openPopup();
      else if (l.o) l.o.openPopup();
    } catch (e) {}
  }

  window.RXMAP = { boot: boot, update: update, resize: resize, focus: focus };
})();
