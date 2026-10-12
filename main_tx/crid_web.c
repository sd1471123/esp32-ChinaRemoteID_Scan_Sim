/**
 * @file crid_web.c
 * @brief 内置配置网页服务器：手机连接热点后访问 http://192.168.4.1
 *        可在线修改 UAS ID / 坐标 / 高度 / 速度 / 巡游参数 / 热点参数
 */

#include "crid_web.h"
#include "crid_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CN_C-RID_WEB";

static cn_crid_config_t *s_config = NULL;

// ================================================================
// 内置网页（全部使用单引号，避免 C 字符串转义）
// 地图双模式：手机有蜂窝数据时加载 Leaflet+OSM 在线地图选点；
// CDN 不可达时自动降级为内置离线网格拾取器（以当前基准点为中心）。
// ================================================================
static const char PAGE_HTML[] =
"<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>C-RID 发射器配置</title>"
"<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>"
"<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js' onerror='window._mapfail=1'></script>"
"<style>"
"*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
"body{font-family:-apple-system,'Segoe UI',Roboto,'PingFang SC','Microsoft YaHei',sans-serif;"
"margin:0;background:#eef1f5;color:#1f2937;line-height:1.5}"
".hdr{background:linear-gradient(135deg,#0b3d2e,#0f8a4d);color:#fff;padding:16px}"
".hdr-in{max-width:1080px;margin:0 auto}"
".hdr h2{margin:0;font-size:20px;display:flex;align-items:center;gap:8px}"
".dot{width:10px;height:10px;border-radius:50%;background:#4ade80;display:inline-block;"
"box-shadow:0 0 6px #4ade80;flex:none}"
".hdr p{margin:4px 0 0;font-size:12px;opacity:.8}"
".wrap{max-width:1080px;margin:0 auto;padding:14px}"
".grid{display:grid;grid-template-columns:1fr;gap:14px}"
".card{background:#fff;border-radius:12px;padding:16px;box-shadow:0 1px 4px rgba(16,24,40,.08)}"
".card h3{margin:0 0 4px;font-size:15px;color:#0b3d2e;display:flex;align-items:center;gap:7px}"
".card h3::before{content:'';width:4px;height:14px;border-radius:2px;background:#0f8a4d;flex:none}"
"label{display:block;font-size:12px;color:#6b7280;margin-top:10px;font-weight:600}"
"input,select{width:100%;padding:9px 10px;margin-top:4px;border:1px solid #d1d5db;"
"border-radius:8px;font-size:15px;background:#f9fafb;color:#1f2937;transition:.15s}"
"input:focus,select:focus{outline:none;border-color:#0f8a4d;background:#fff;"
"box-shadow:0 0 0 3px rgba(15,138,77,.15)}"
".g{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}"
".note{font-size:12px;color:#9ca3af;margin:8px 0 0}"
"#map{height:320px;border-radius:10px;border:1px solid #e5e7eb;z-index:0}"
"#grid{width:100%;border-radius:10px;border:1px solid #e5e7eb;touch-action:manipulation}"
".seg{display:flex;gap:8px;margin:8px 0 10px}"
".seg label{margin:0;flex:1}"
".seg input{display:none}"
".seg span{display:block;text-align:center;padding:8px;border-radius:8px;"
"border:1px solid #d1d5db;font-size:14px;font-weight:600;color:#374151;"
"background:#f9fafb;cursor:pointer;transition:.15s}"
".seg input:checked+span{background:#0f8a4d;border-color:#0f8a4d;color:#fff}"
".mpp{margin-top:10px}"
".dd{position:relative}"
".dd-btn{display:flex;justify-content:space-between;align-items:center;width:100%;"
"padding:9px 12px;margin-top:4px;border:1px solid #d1d5db;border-radius:8px;"
"background:#f9fafb;font-size:15px;color:#374151;cursor:pointer;transition:.15s;text-align:left}"
".dd-btn:hover{border-color:#0f8a4d}"
".dd.open .dd-btn{border-color:#0f8a4d;background:#fff;box-shadow:0 0 0 3px rgba(15,138,77,.15)}"
".dd-txt.ph{color:#9ca3af}"
".dd-car{color:#6b7280;font-size:12px;transition:.2s}"
".dd.open .dd-car{transform:rotate(180deg)}"
".dd-panel{display:none;position:absolute;z-index:60;left:0;right:0;top:calc(100% + 4px);"
"background:#fff;border:1px solid #e5e7eb;border-radius:10px;"
"box-shadow:0 8px 24px rgba(16,24,40,.16);overflow:hidden}"
".dd.open .dd-panel{display:block}"
".dd-list{max-height:240px;overflow-y:auto;padding:4px;-webkit-overflow-scrolling:touch}"
".dd-group{font-size:11px;font-weight:700;color:#9ca3af;padding:8px 10px 2px}"
".dd-item{padding:9px 10px;border-radius:7px;font-size:14px;color:#1f2937;cursor:pointer}"
".dd-item:hover{background:#ecfdf5;color:#0b3d2e}"
"#liveinfo{font-size:12px;color:#1565c0;margin-top:8px;min-height:14px;"
"font-family:monospace;background:#eff6ff;border-radius:6px;padding:5px 8px}"
"#hint{font-size:12px;color:#9ca3af;margin-top:8px}"
".actions{display:flex;gap:10px}"
"button{flex:1;padding:13px;border:0;border-radius:10px;cursor:pointer;transition:.15s;"
"background:linear-gradient(135deg,#0f8a4d,#0a7d43);color:#fff;font-size:16px;font-weight:600}"
"button:hover{filter:brightness(1.08)}"
"button:active{transform:scale(.98)}"
"button.danger{flex:0 0 34%;background:#fff;color:#c0392b;border:1px solid #f3b6b0}"
"button.danger:hover{background:#fdf1f0;filter:none}"
"#msg{margin-top:10px;font-size:14px;min-height:18px;font-weight:600}"
".foot{font-size:12px;color:#9ca3af;margin:10px 2px}"
"@media(min-width:860px){"
".grid{grid-template-columns:1fr 1fr}"
".span2{grid-column:1/-1}"
"#map{height:460px}"
"}"
"</style></head><body>"
"<div class='hdr'><div class='hdr-in'>"
"<h2><span class='dot'></span>C-RID 发射器配置</h2>"
"<p>无人机 Remote ID 广播参数在线配置</p>"
"</div></div>"
"<div class='wrap'><div class='grid'>"
"<div class='card'><h3>身份信息</h3>"
"<label>无人机唯一 ID (UAS ID)</label><input id='uas_id' maxlength='24'>"
"<label>机型名称 (Self-ID)</label><input id='drone_name' maxlength='20'>"
"<label>机型代码 (UA Type)</label>"
"<div class='dd' id='dd2'>"
"<div class='dd-btn' id='dd2btn'><span class='dd-txt ph' id='dd2txt'>-- 选择机型代码 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='dd2list'></div></div></div>"
"<input type='hidden' id='ua_type'>"
"<label>大疆机型预设 (伪装)</label>"
"<div class='dd' id='dd'>"
"<div class='dd-btn' id='ddbtn'><span class='dd-txt ph' id='ddtxt'>-- 选择型号自动填充 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='ddlist'></div></div></div>"
"<p class='note'>预设按大疆 RID 序列号前 8 位（1581F + 型号码）生成 20 位序列号，并填入机型名称与机型代码；机型代码为 RID 标准 16 类航空器类别码。序列号可手动微调，其他品牌用「其他厂商」分组。</p>"
"<label>操作员 ID</label><input id='operator_id' maxlength='20'>"
"<label>热点名称 (SSID)</label><input id='ssid' maxlength='31'>"
"<div class='g'>"
"<div><label>Wi-Fi 信道 (1-13)</label><input id='channel' type='number' min='1' max='13'></div>"
"<div><label>广播间隔 (ms)</label><input id='beacon_interval_ms' type='number' min='200' max='60000'></div>"
"</div></div>"
"<div class='card'><h3>飞行参数</h3><div class='g'>"
"<div><label>基准纬度</label><input id='base_latitude' type='number' step='0.0000001'></div>"
"<div><label>基准经度</label><input id='base_longitude' type='number' step='0.0000001'></div>"
"<div><label>海拔高度 m (MSL)</label><input id='base_altitude_msl' type='number'></div>"
"<div><label>相对高度 m (AGL)</label><input id='altitude_agl' type='number'></div>"
"<div><label>水平速度 m/s</label><input id='speed_horizontal' type='number' step='0.1'></div>"
"<div><label>运行状态</label>"
"<div class='dd' id='dd3'>"
"<div class='dd-btn' id='dd3btn'><span class='dd-txt ph' id='dd3txt'>-- 选择状态 --</span>"
"<span class='dd-car'>&#9662;</span></div>"
"<div class='dd-panel'><div class='dd-list' id='dd3list'></div></div></div>"
"<input type='hidden' id='status'></div>"
"</div></div>"
"<div class='card'><h3>飞手位置</h3><div class='g'>"
"<div><label>飞手纬度</label><input id='operator_latitude' type='number' step='0.0000001'></div>"
"<div><label>飞手经度</label><input id='operator_longitude' type='number' step='0.0000001'></div>"
"</div>"
"<label>飞手海拔高度 m</label><input id='operator_altitude' type='number'>"
"<p class='note'>修改飞手坐标后，无人机位置自动按默认相对偏移生成；单独修改无人机位置不影响飞手。</p></div>"
"<div class='card'><h3>巡游参数</h3><div class='g'>"
"<div><label>巡游速度系数</label><input id='patrol_speed' type='number' step='0.01'></div>"
"<div><label>纬度半径 (度)</label><input id='patrol_radius_lat' type='number' step='0.00001'></div>"
"<div><label>经度半径 (度)</label><input id='patrol_radius_lon' type='number' step='0.00001'></div>"
"</div></div>"
"<div class='card span2'><h3>地图选点</h3>"
"<div class='seg'>"
"<label><input type='radio' name='pt' value='op' checked><span>选点：飞手</span></label>"
"<label><input type='radio' name='pt' value='drone'><span>选点：无人机</span></label>"
"</div>"
"<div id='map'></div>"
"<div id='gridbox' style='display:none'>"
"<canvas id='grid' width='320' height='320'></canvas>"
"<div class='mpp'><label>比例尺 (米/像素)</label><select id='mpp'>"
"<option value='5'>5</option><option value='20' selected>20</option>"
"<option value='100'>100</option><option value='500'>500</option></select></div>"
"</div>"
"<div id='liveinfo'></div>"
"<div id='hint'>红点=飞手，绿点=无人机，蓝点=实时广播位置，绿圈=巡游范围。"
"地图以飞手位置为中心：选点对象为「飞手」时点击/拖动会同步按默认偏移生成无人机位置；"
"选「无人机」时只改无人机位置。在线地图需手机保持移动数据，无网络时自动切换离线网格。</div>"
"</div>"
"<div class='actions span2'>"
"<button onclick='save()'>保存并应用</button>"
"<button class='danger' onclick='resetCfg()'>恢复初始</button>"
"</div>"
"<div id='msg' class='span2'></div>"
"</div>"
"<p class='foot'>手机连接本机热点后访问 http://192.168.4.1 打开此页。"
"修改 SSID / 信道 / 广播间隔保存后需重新连接热点。配置已持久化，断电不丢失。</p>"
"</div>"
"<script>"
"const F=['uas_id','drone_name','operator_id','ssid','channel','beacon_interval_ms','ua_type',"
"'base_latitude','base_longitude','base_altitude_msl','altitude_agl','speed_horizontal',"
"'operator_latitude','operator_longitude','operator_altitude',"
"'status','patrol_speed','patrol_radius_lat','patrol_radius_lon'];"
"function msg(t,ok){const m=document.getElementById('msg');m.textContent=t;"
"m.style.color=ok?'#0a7d43':'#c0392b';}"
"function fld(k){return document.getElementById(k);}"
"function baseLL(){const la=parseFloat(fld('base_latitude').value)||0;"
"const lo=parseFloat(fld('base_longitude').value)||0;return [la,lo];}"
"function opLL(){const la=parseFloat(fld('operator_latitude').value)||0;"
"const lo=parseFloat(fld('operator_longitude').value)||0;return [la,lo];}"
// 无人机基准点相对飞手的默认偏移（度），取自 crid_config.c 出厂默认值之差
"const D_OFF=[0.00287,0.00026];"
"function pickLL(la,lo){fld('base_latitude').value=la.toFixed(7);"
"fld('base_longitude').value=lo.toFixed(7);}"
"function autoDrone(la,lo){pickLL(la+D_OFF[0],lo+D_OFF[1]);}"
"function ptarget(){const r=document.querySelector('input[name=pt]:checked');"
"return r?r.value:'op';}"
"function syncMarkers(){const d=baseLL(),o=opLL();"
"if(opMarker)opMarker.setLatLng(o);"
"if(dMarker)dMarker.setLatLng(d);"
"if(map&&opMarker)map.panTo(o);"
"drawCircle();"
"if(!map){gbase=o;gpick=null;drawGrid();}}"
"function applyPick(t,la,lo){"
"if(t==='op'){fld('operator_latitude').value=la.toFixed(7);"
"fld('operator_longitude').value=lo.toFixed(7);autoDrone(la,lo);}"
"else{pickLL(la,lo);}"
"syncMarkers();}"
// 手动编辑飞手坐标输入框时，实时自动生成无人机位置
"function bindAuto(id){fld(id).addEventListener('input',function(){"
"const la=parseFloat(fld('operator_latitude').value);"
"const lo=parseFloat(fld('operator_longitude').value);"
"if(!isNaN(la)&&!isNaN(lo)){autoDrone(la,lo);syncMarkers();}});}"
"bindAuto('operator_latitude');bindAuto('operator_longitude');"
// ---- 大疆机型预设：前 8 位序列号前缀 -> 型号（数据源自社区逆向汇总，1581F 为大疆统一前缀）----
"const DJI=["
"['消费级',[['1581F8LQ','Mavic 4 Pro'],['1581F67Q','Mavic 3 Pro'],['1581F5Y8','Mavic 3 Classic'],"
"['1581F45Q','Mavic 3'],['1581F45T','Mavic 3'],['1581F895','Air 3S'],['1581F6N8','Air 3'],"
"['1581F385','Air 2S'],['1581FANL','Mini 5 Pro'],['1581F9DE','Mini 5 Pro'],['1581F5QJ','Mini 4 Pro'],"
"['1581F6Z9','Mini 4 Pro'],['1581F8C8','Mini 4K'],['1581F4XF','Mini 3 Pro'],['1581F5YH','Mini 3'],"
"['1581F6CD','Mini 2 SE'],['1581FA8J','Avata 360'],['1581FBV5','Lito 1'],['1581FB34','Lito X1']]],"
"['穿越机',[['1581F6W8','Avata 2'],['1581F4CQ','Avata'],['1581F4QW','Avata'],['1581FA6Q','Neo 2'],"
"['1581F8A1','Neo'],['1581F87L','Neo'],['1581F3CQ','DJI FPV'],['1581F7V2','DJI Flip']]],"
"['行业级',[['1581F6H8','Matrice 350 RTK'],['1581F5BK','Matrice 30'],['1581F5BM','Matrice 30T'],"
"['1581F52Q','Mavic 3E/3T'],['1581F5FH','Mavic 3E/3T'],['1581F5FJ','Mavic 3M'],['1581F578','Inspire 3'],"
"['1581F4Z4','Inspire 3'],['1581F6GK','Matrice 300 Series'],['1581F6Q8','Matrice 3D'],"
"['1581F6QA','Matrice 3TD'],['1581F9HE','Matrice 4T/4E'],['1581F7K3','Matrice 4T/4E'],"
"['1581F8HH','Matrice 4D/TD'],['1581F8HG','Matrice 4D/TD'],['1581F8DB','Matrice 400'],"
"['1581F986','Mavic 4 Series'],['1581FAN4','FlyCart 100']]],"
"['其他厂商',[['1748FEV3','Autel EVO MAX']]]];"
"(function(){const dd=fld('dd'),bt=fld('ddbtn'),tx=fld('ddtxt'),ls=fld('ddlist');"
"function pick(nm){const ps=[];"
"DJI.forEach(function(g){g[1].forEach(function(m){if(m[1]===nm)ps.push(m[0]);});});"
"const p=ps[Math.floor(Math.random()*ps.length)];"
"const cs='ABCDEFGHJKLMNPQRSTUVWXYZ0123456789';let sf='';"
"for(let i=0;i<12;i++){sf+=cs[Math.floor(Math.random()*cs.length)];}"
"fld('uas_id').value=(p+sf).substring(0,20);"
"fld('drone_name').value=nm;"
"fld('ua_type').value='2';uatSync();"
"tx.textContent=nm;tx.classList.remove('ph');"
"msg('已按 '+nm+' 生成预设（前缀 '+p+'）：序列号/机型名称/机型代码已填充，可手动微调后保存',true);}"
"DJI.forEach(function(g){const hd=document.createElement('div');"
"hd.className='dd-group';hd.textContent=g[0];ls.appendChild(hd);"
"const seen={};g[1].forEach(function(m){if(seen[m[1]])return;seen[m[1]]=1;"
"const it=document.createElement('div');it.className='dd-item';it.textContent=m[1];"
"it.onclick=function(){pick(m[1]);dd.classList.remove('open');};"
"ls.appendChild(it);});});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
// ---- 运行状态下拉 ----
"const ST=[['0','未声明'],['1','地面'],['2','空中'],['3','紧急'],['4','识别故障']];"
"function statusSync(){const v=String(fld('status').value);let lb='';"
"ST.forEach(function(u){if(u[0]===v)lb=u[1];});"
"const t=fld('dd3txt');"
"if(lb){t.textContent=lb;t.classList.remove('ph');}"
"else{t.textContent='-- 选择状态 --';t.classList.add('ph');}}"
"(function(){const dd=fld('dd3'),bt=fld('dd3btn'),ls=fld('dd3list');"
"ST.forEach(function(u){const it=document.createElement('div');it.className='dd-item';"
"it.textContent=u[1];"
"it.onclick=function(){fld('status').value=u[0];statusSync();dd.classList.remove('open');};"
"ls.appendChild(it);});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
"const UAT=[['0','未声明 (None)'],['1','固定翼 (Aeroplane)'],['2','多旋翼/直升机 (Multirotor)'],"
"['3','旋翼机 (Gyroplane)'],['4','垂直起降固定翼 (VTOL)'],['5','扑翼机 (Ornithopter)'],"
"['6','滑翔机 (Glider)'],['7','风筝 (Kite)'],['8','自由气球 (Free Balloon)'],"
"['9','系留气球 (Captive Balloon)'],['10','飞艇 (Airship)'],['11','降落伞 (Free Fall/Parachute)'],"
"['12','火箭 (Rocket)'],['13','系留动力飞行器 (Tethered)'],['14','地面障碍物 (Ground Obstacle)'],"
"['15','其他 (Other)']];"
"function uatSync(){const v=String(fld('ua_type').value);let lb='';"
"UAT.forEach(function(u){if(u[0]===v)lb=u[1];});"
"const t=fld('dd2txt');"
"if(lb){t.textContent=v+' - '+lb;t.classList.remove('ph');}"
"else{t.textContent='-- 选择机型代码 --';t.classList.add('ph');}}"
"(function(){const dd=fld('dd2'),bt=fld('dd2btn'),ls=fld('dd2list');"
"UAT.forEach(function(u){const it=document.createElement('div');it.className='dd-item';"
"it.textContent=u[0]+' - '+u[1];"
"it.onclick=function(){fld('ua_type').value=u[0];uatSync();dd.classList.remove('open');};"
"ls.appendChild(it);});"
"bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};"
"ls.onclick=function(e){e.stopPropagation();};"
"document.addEventListener('click',function(){dd.classList.remove('open');});})();"
"async function load(){try{const r=await fetch('/api/config');const j=await r.json();"
"F.forEach(k=>{if(j[k]!==undefined&&j[k]!==null)fld(k).value=j[k];});"
"uatSync();statusSync();initMap();}"
"catch(e){msg('加载失败: '+e,false);}}"
"const NUM=['channel','beacon_interval_ms','ua_type','base_latitude','base_longitude',"
"'base_altitude_msl','operator_latitude','operator_longitude','operator_altitude',"
"'altitude_agl','speed_horizontal','status',"
"'patrol_speed','patrol_radius_lat','patrol_radius_lon'];"
"async function save(){const d={};F.forEach(function(k){"
"const v=fld(k).value;"
"d[k]=NUM.indexOf(k)>=0?(v===''?null:parseFloat(v)):v;});"
"try{const r=await fetch('/api/config',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});"
"const j=await r.json();"
"if(j.ok){msg('已保存并应用 ✓'+(j.restarted?' (热点参数已更新，如 SSID/信道变化请重新连接)':''),true);"
"if(circle){map.removeLayer(circle);circle=null;}drawCircle();}"
"else{msg('保存失败: '+(j.err||''),false);}}"
"catch(e){msg('保存失败: '+e,false);}}"
"async function resetCfg(){if(!confirm('确认恢复全部初始设定？当前配置将被覆盖。'))return;"
"try{const r=await fetch('/api/reset',{method:'POST'});const j=await r.json();"
"if(j.ok){msg('已恢复初始设定 ✓'+(j.restarted?' (热点参数变化，请重新连接热点)':''),true);"
"await load();}else{msg('恢复失败',false);}}"
"catch(e){msg('恢复失败: '+e,false);}}"
// ---- 在线地图（Leaflet）与离线网格双模式 ----
"let map=null,opMarker=null,dMarker=null,live=null,circle=null,ginit=false;"
"let gbase=[0,0],gpick=null;"
"function dotIcon(c){return L.divIcon({className:'',"
"html:'<div style=\"width:14px;height:14px;border-radius:50%;background:'+c+"
"';border:2px solid #fff;box-shadow:0 1px 3px rgba(0,0,0,.4)\"></div>',"
"iconSize:[14,14],iconAnchor:[7,7]});}"
"function drawCircle(){if(!map)return;if(circle){map.removeLayer(circle);circle=null;}"
"const r=(parseFloat(fld('patrol_radius_lat').value)||0)*111320;"
"if(r>0&&dMarker){circle=L.circle(dMarker.getLatLng(),{radius:r,color:'#0a7d43',"
"weight:1,fillOpacity:.05}).addTo(map);}}"
"function initMap(){"
"if(ginit){syncMarkers();return;}"
"ginit=true;const o=opLL(),d=baseLL();"
"if(typeof L!=='undefined'&&!window._mapfail){"
"map=L.map('map').setView(o,15);"                     // 地图默认以飞手位置为中心
"L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',"
"{maxZoom:19,attribution:'&copy; OpenStreetMap'}).addTo(map);"
"opMarker=L.marker(o,{draggable:true,icon:dotIcon('#c0392b')}).addTo(map);"
"dMarker=L.marker(d,{draggable:true,icon:dotIcon('#0a7d43')}).addTo(map);"
"opMarker.on('dragend',function(e){const p=opMarker.getLatLng();"
"applyPick('op',p.lat,p.lng);});"
"dMarker.on('dragend',function(e){const p=dMarker.getLatLng();"
"applyPick('drone',p.lat,p.lng);});"
"map.on('click',function(e){applyPick(ptarget(),e.latlng.lat,e.latlng.lng);});"
"live=L.circleMarker(d,{radius:6,color:'#1565c0',fillColor:'#1565c0',"
"fillOpacity:.9}).addTo(map);"
"drawCircle();}"
"else{document.getElementById('map').style.display='none';"
"document.getElementById('gridbox').style.display='block';"
"gbase=o;drawGrid();"                                  // 离线网格同样以飞手为中心
"document.getElementById('mpp').onchange=function(){drawGrid();};"
"const cv=document.getElementById('grid');"
"cv.addEventListener('click',function(e){"
"const rc=cv.getBoundingClientRect();const s=320/rc.width;"
"const x=(e.clientX-rc.left)*s,y=(e.clientY-rc.top)*s;"
"const mpp=parseFloat(fld('mpp').value);"
"const dx=(x-160)*mpp,dy=(160-y)*mpp;"
"const la=gbase[0]+dy/111320;"
"const lo=gbase[1]+dx/(111320*Math.cos(gbase[0]*Math.PI/180));"
"applyPick(ptarget(),la,lo);gpick=[la,lo];drawGrid();});}}"
"function drawGrid(){const cv=document.getElementById('grid');"
"if(!cv)return;"
"const x=cv.getContext('2d');"
"x.fillStyle='#fafafa';x.fillRect(0,0,320,320);"
"x.strokeStyle='#e2e2e2';"
"for(let i=0;i<=320;i+=40){x.beginPath();x.moveTo(i,0);x.lineTo(i,320);x.stroke();"
"x.beginPath();x.moveTo(0,i);x.lineTo(320,i);x.stroke();}"
"x.strokeStyle='#999';x.beginPath();x.moveTo(160,0);x.lineTo(160,320);"
"x.moveTo(0,160);x.lineTo(320,160);x.stroke();"
"x.fillStyle='#555';x.font='11px sans-serif';"
"x.fillText(fld('mpp').value+'米/像素',8,312);"
"x.fillStyle='#0a7d43';x.beginPath();x.arc(160,160,6,0,6.3);x.fill();"
"x.fillStyle='#0a7d43';x.fillText('中心=基准点',170,164);"
"if(gpick){const mpp=parseFloat(fld('mpp').value);"
"const px=160+(gpick[1]-gbase[1])*111320*Math.cos(gbase[0]*Math.PI/180)/mpp;"
"const py=160-(gpick[0]-gbase[0])*111320/mpp;"
"x.fillStyle='#c0392b';x.beginPath();x.arc(px,py,6,0,6.3);x.fill();}}"
// ---- 实时广播位置轮询 ----
"async function poll(){try{const r=await fetch('/api/status');const j=await r.json();"
"const t='实时广播位置: '+j.latitude.toFixed(6)+', '+j.longitude.toFixed(6)+"
"'  高度 '+j.altitude_msl.toFixed(1)+'m  航向 '+j.heading.toFixed(0)+'°';"
"document.getElementById('liveinfo').textContent=t;"
"if(live){live.setLatLng([j.latitude,j.longitude]);}}"
"catch(e){}}"
"setInterval(poll,2000);"
"load();"
"</script></body></html>";

// ================================================================
// URI 处理
// ================================================================

static esp_err_t handler_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_get_config(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cJSON *j = cJSON_CreateObject();
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(j, "uas_id", cfg->uas_id);
    cJSON_AddStringToObject(j, "drone_name", cfg->drone_name);
    cJSON_AddStringToObject(j, "operator_id", cfg->operator_id);
    cJSON_AddStringToObject(j, "ssid", cfg->ssid);
    cJSON_AddNumberToObject(j, "channel", cfg->channel);
    cJSON_AddNumberToObject(j, "beacon_interval_ms", cfg->beacon_interval_ms);
    cJSON_AddNumberToObject(j, "ua_type", cfg->ua_type);
    cJSON_AddNumberToObject(j, "base_latitude", cfg->base_latitude);
    cJSON_AddNumberToObject(j, "base_longitude", cfg->base_longitude);
    cJSON_AddNumberToObject(j, "base_altitude_msl", cfg->base_altitude_msl);
    cJSON_AddNumberToObject(j, "operator_latitude", cfg->operator_lat);
    cJSON_AddNumberToObject(j, "operator_longitude", cfg->operator_lon);
    cJSON_AddNumberToObject(j, "operator_altitude", cfg->operator_alt);
    cJSON_AddNumberToObject(j, "altitude_agl", cfg->altitude_agl);
    cJSON_AddNumberToObject(j, "speed_horizontal", cfg->speed_horizontal);
    cJSON_AddNumberToObject(j, "status", cfg->status);
    cJSON_AddNumberToObject(j, "patrol_speed", cfg->patrol_speed);
    cJSON_AddNumberToObject(j, "patrol_radius_lat", cfg->patrol_radius_lat);
    cJSON_AddNumberToObject(j, "patrol_radius_lon", cfg->patrol_radius_lon);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 从 JSON 取浮点/整数（不存在则跳过）。
// 兼容数字与字符串两种类型：浏览器表单提交的值常为字符串（"23.14"）。
static bool jget_num(const cJSON *j, const char *key, double *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(j, key);
    if (it == NULL) return false;
    if (cJSON_IsNumber(it)) {
        *out = cJSON_GetNumberValue(it);
        return true;
    }
    if (cJSON_IsString(it)) {
        const char *s = cJSON_GetStringValue(it);
        if (s != NULL && *s != '\0') {
            char *end = NULL;
            double v = strtod(s, &end);
            if (end != s) { *out = v; return true; }
        }
    }
    return false;
}

static esp_err_t handler_post_config(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }

    char *body = malloc(total + 1);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int received = 0, ret;
    while (received < total) {
        ret = httpd_req_recv(req, body + received, total - received);
        if (ret <= 0) { free(body); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv fail"); return ESP_FAIL; }
        received += ret;
    }
    body[total] = '\0';

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    cn_crid_config_t *cfg = s_config;
    cn_crid_config_t next = *cfg;       // 先在副本上校验，全部通过后才应用
    char err[64] = {0};
    double v;
    bool restarted = false;

    const cJSON *item;
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "uas_id")) != NULL && cJSON_IsString(item)) {
        strncpy(next.uas_id, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.uas_id[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "drone_name")) != NULL && cJSON_IsString(item)) {
        strncpy(next.drone_name, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.drone_name[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "operator_id")) != NULL && cJSON_IsString(item)) {
        strncpy(next.operator_id, cJSON_GetStringValue(item), CRID_UAS_ID_MAX_LEN);
        next.operator_id[CRID_UAS_ID_MAX_LEN] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(j, "ssid")) != NULL && cJSON_IsString(item)) {
        const char *s = cJSON_GetStringValue(item);
        if (strlen(s) == 0) snprintf(err, sizeof(err), "SSID 不能为空");
        else { strncpy(next.ssid, s, CRID_SSID_MAX_LEN); next.ssid[CRID_SSID_MAX_LEN] = '\0'; }
    }
    if (err[0] == '\0' && jget_num(j, "channel", &v)) {
        if (v < 1 || v > 13) snprintf(err, sizeof(err), "信道须在 1-13");
        else next.channel = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "beacon_interval_ms", &v)) {
        if (v < 200 || v > 60000) snprintf(err, sizeof(err), "广播间隔须在 200-60000ms");
        else next.beacon_interval_ms = (uint16_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "ua_type", &v)) {
        if (v < 0 || v > 15) snprintf(err, sizeof(err), "机型代码须在 0-15");
        else next.ua_type = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_latitude", &v)) {
        if (v < -90 || v > 90) snprintf(err, sizeof(err), "纬度须在 -90~90");
        else next.base_latitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_longitude", &v)) {
        if (v < -180 || v > 180) snprintf(err, sizeof(err), "经度须在 -180~180");
        else next.base_longitude = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_latitude", &v)) {
        if (v < -90 || v > 90) snprintf(err, sizeof(err), "飞手纬度须在 -90~90");
        else next.operator_lat = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_longitude", &v)) {
        if (v < -180 || v > 180) snprintf(err, sizeof(err), "飞手经度须在 -180~180");
        else next.operator_lon = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "operator_altitude", &v)) {
        if (v < -1000 || v > 10000) snprintf(err, sizeof(err), "飞手海拔须在 -1000~10000m");
        else next.operator_alt = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "base_altitude_msl", &v)) next.base_altitude_msl = (float)v;
    if (err[0] == '\0' && jget_num(j, "altitude_agl", &v)) next.altitude_agl = (float)v;
    if (err[0] == '\0' && jget_num(j, "speed_horizontal", &v)) {
        if (v < 0 || v > 254) snprintf(err, sizeof(err), "速度须在 0~254 m/s");
        else next.speed_horizontal = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "status", &v)) {
        if (v < 0 || v > 4) snprintf(err, sizeof(err), "状态取值 0-4");
        else next.status = (uint8_t)v;
    }
    if (err[0] == '\0' && jget_num(j, "patrol_speed", &v)) {
        if (v < 0 || v > 10) snprintf(err, sizeof(err), "巡游速度系数须在 0~10");
        else next.patrol_speed = (float)v;
    }
    if (err[0] == '\0' && jget_num(j, "patrol_radius_lat", &v)) next.patrol_radius_lat = (float)v;
    if (err[0] == '\0' && jget_num(j, "patrol_radius_lon", &v)) next.patrol_radius_lon = (float)v;
    cJSON_Delete(j);

    if (err[0] != '\0') {
        httpd_resp_set_type(req, "application/json");
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", err);
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    // 校验通过，应用配置
    bool ap_changed = (strcmp(next.ssid, cfg->ssid) != 0) ||
                      (next.channel != cfg->channel) ||
                      (next.beacon_interval_ms != cfg->beacon_interval_ms);
    uint8_t saved_counter = cfg->message_counter;   // 计数器不回退
    *cfg = next;
    cfg->message_counter = saved_counter;
    // 同步当前显示位置
    cfg->latitude = cfg->base_latitude;
    cfg->longitude = cfg->base_longitude;
    cfg->altitude_msl = cfg->base_altitude_msl;

    if (ap_changed) {
        if (crid_wifi_update_ap(cfg->ssid, cfg->channel, cfg->beacon_interval_ms) == ESP_OK) {
            restarted = true;
        }
    }

    crid_config_save_nvs(cfg);
    ESP_LOGI(TAG, "Config updated via web: id=%s pos=(%.6f,%.6f) restarted=%d",
             cfg->uas_id, cfg->base_latitude, cfg->base_longitude, restarted);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, restarted ? "{\"ok\":true,\"restarted\":true}"
                                      : "{\"ok\":true,\"restarted\":false}");
    return ESP_OK;
}

// 实时广播位置（供网页地图显示蓝点，每 2 秒被前端轮询）
static esp_err_t handler_status(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cJSON *j = cJSON_CreateObject();
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(j, "latitude", cfg->latitude);
    cJSON_AddNumberToObject(j, "longitude", cfg->longitude);
    cJSON_AddNumberToObject(j, "altitude_msl", cfg->altitude_msl);
    cJSON_AddNumberToObject(j, "heading", cfg->heading);
    cJSON_AddNumberToObject(j, "speed", cfg->speed_horizontal);

    const char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    cJSON_free((void *)s);
    cJSON_Delete(j);
    return ESP_OK;
}

// 恢复出厂默认配置（保留消息计数器，防止接收端乱序）
static esp_err_t handler_reset(httpd_req_t *req) {
    cn_crid_config_t *cfg = s_config;
    cn_crid_config_t next;
    crid_config_init_default(&next);

    bool ap_changed = (strcmp(next.ssid, cfg->ssid) != 0) ||
                      (next.channel != cfg->channel) ||
                      (next.beacon_interval_ms != cfg->beacon_interval_ms);
    uint8_t saved_counter = cfg->message_counter;
    *cfg = next;
    cfg->message_counter = saved_counter;
    cfg->latitude = cfg->base_latitude;
    cfg->longitude = cfg->base_longitude;
    cfg->altitude_msl = cfg->base_altitude_msl;

    bool restarted = false;
    if (ap_changed) {
        if (crid_wifi_update_ap(cfg->ssid, cfg->channel, cfg->beacon_interval_ms) == ESP_OK) {
            restarted = true;
        }
    }
    crid_config_save_nvs(cfg);
    ESP_LOGW(TAG, "Config reset to factory defaults via web (restarted=%d)", restarted);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, restarted ? "{\"ok\":true,\"restarted\":true}"
                                      : "{\"ok\":true,\"restarted\":false}");
    return ESP_OK;
}

// 浏览器会自动请求站点图标，返回 204 空响应避免 404 日志刷屏
static esp_err_t handler_favicon(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uris[] = {
    { .uri = "/",          .method = HTTP_GET,  .handler = handler_page,      .user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_GET,  .handler = handler_get_config,.user_ctx = NULL },
    { .uri = "/api/config",.method = HTTP_POST, .handler = handler_post_config,.user_ctx = NULL },
    { .uri = "/api/status",.method = HTTP_GET,  .handler = handler_status,    .user_ctx = NULL },
    { .uri = "/api/reset", .method = HTTP_POST, .handler = handler_reset,     .user_ctx = NULL },
    { .uri = "/favicon.ico",.method = HTTP_GET, .handler = handler_favicon,   .user_ctx = NULL },
};

esp_err_t crid_web_start(cn_crid_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = config;

    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 8;
    http_cfg.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &http_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "Web config server started: http://192.168.4.1");
    return ESP_OK;
}
