
let map=null,opMarker=null,dMarker=null,live=null,circle=null,ginit=false;
let trailPts=[],trailLine=null;
const F=['uas_id','drone_name','operator_id','realname_id','ssid','channel','beacon_interval_ms','ua_type',
'base_latitude','base_longitude','base_altitude_msl','altitude_agl','speed_horizontal',
'operator_latitude','operator_longitude','operator_altitude',
'status','patrol_speed','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];
function msg(t,ok){const m=document.getElementById('msg');m.textContent=t;
m.style.color=ok?'#0a7d43':'#c0392b';}
function fld(k){return document.getElementById(k);}
function baseLL(){const la=parseFloat(fld('base_latitude').value)||0;
const lo=parseFloat(fld('base_longitude').value)||0;return [la,lo];}
function opLL(){const la=parseFloat(fld('operator_latitude').value)||0;
const lo=parseFloat(fld('operator_longitude').value)||0;return [la,lo];}
const D_OFF=[0.00287,0.00026];
function pickLL(la,lo){fld('base_latitude').value=la.toFixed(7);
fld('base_longitude').value=lo.toFixed(7);}
function autoDrone(la,lo){pickLL(la+D_OFF[0],lo+D_OFF[1]);}
function ptarget(){const r=document.querySelector('input[name=pt]:checked');
return r?r.value:'op';}
function syncMarkers(){const d=baseLL(),o=opLL();
if(opMarker)opMarker.setLatLng(o);
if(dMarker)dMarker.setLatLng(d);
if(map&&opMarker)map.panTo(o);
if(typeof drawCircle==='function')drawCircle();
if(!map){gbase=o;gpick=null;if(typeof drawGrid==='function')drawGrid();}}
function applyPick(t,la,lo){
if(t==='op'){fld('operator_latitude').value=la.toFixed(7);
fld('operator_longitude').value=lo.toFixed(7);autoDrone(la,lo);}
else{pickLL(la,lo);}
syncMarkers();}
function bindAuto(id){fld(id).addEventListener('input',function(){
const la=parseFloat(fld('operator_latitude').value);
const lo=parseFloat(fld('operator_longitude').value);
if(!isNaN(la)&&!isNaN(lo)){autoDrone(la,lo);syncMarkers();}});}
bindAuto('operator_latitude');bindAuto('operator_longitude');
const DJI=[
['消费级',[['1581F8LQ','Mavic 4 Pro'],['1581F67Q','Mavic 3 Pro'],['1581F5Y8','Mavic 3 Classic'],
['1581F45Q','Mavic 3'],['1581F45T','Mavic 3'],['1581F895','Air 3S'],['1581F6N8','Air 3'],
['1581F385','Air 2S'],['1581FANL','Mini 5 Pro'],['1581F9DE','Mini 5 Pro'],['1581F5QJ','Mini 4 Pro'],
['1581F6Z9','Mini 4 Pro'],['1581F8C8','Mini 4K'],['1581F4XF','Mini 3 Pro'],['1581F5YH','Mini 3'],
['1581F6CD','Mini 2 SE'],['1581FA8J','Avata 360'],['1581FBV5','Lito 1'],['1581FB34','Lito X1']]],
['穿越机',[['1581F6W8','Avata 2'],['1581F4CQ','Avata'],['1581F4QW','Avata'],['1581FA6Q','Neo 2'],
['1581F8A1','Neo'],['1581F87L','Neo'],['1581F3CQ','DJI FPV'],['1581F7V2','DJI Flip']]],
['行业级',[['1581F6H8','Matrice 350 RTK'],['1581F5BK','Matrice 30'],['1581F5BM','Matrice 30T'],
['1581F52Q','Mavic 3E/3T'],['1581F5FH','Mavic 3E/3T'],['1581F5FJ','Mavic 3M'],['1581F578','Inspire 3'],
['1581F4Z4','Inspire 3'],['1581F6GK','Matrice 300 Series'],['1581F6Q8','Matrice 3D'],
['1581F6QA','Matrice 3TD'],['1581F9HE','Matrice 4T/4E'],['1581F7K3','Matrice 4T/4E'],
['1581F8HH','Matrice 4D/TD'],['1581F8HG','Matrice 4D/TD'],['1581F8DB','Matrice 400'],
['1581F986','Mavic 4 Series'],['1581FAN4','FlyCart 100']]],
['其他厂商',[['1748FEV3','Autel EVO MAX']]]];
(function(){const dd=fld('dd'),bt=fld('ddbtn'),tx=fld('ddtxt'),ls=fld('ddlist');
function pick(nm){const ps=[];
DJI.forEach(function(g){g[1].forEach(function(m){if(m[1]===nm)ps.push(m[0]);});});
const p=ps[Math.floor(Math.random()*ps.length)];
const cs='ABCDEFGHJKLMNPQRSTUVWXYZ0123456789';let sf='';
for(let i=0;i<12;i++){sf+=cs[Math.floor(Math.random()*cs.length)];}
fld('uas_id').value=(p+sf).substring(0,20);
fld('drone_name').value=nm;
fld('ua_type').value='2';uatSync();
tx.textContent=nm;tx.classList.remove('ph');
msg('已按 '+nm+' 生成预设（前缀 '+p+'）：序列号/机型名称/机型代码已填充，可手动微调后保存',true);}
DJI.forEach(function(g){const hd=document.createElement('div');
hd.className='dd-group';hd.textContent=g[0];ls.appendChild(hd);
const seen={};g[1].forEach(function(m){if(seen[m[1]])return;seen[m[1]]=1;
const it=document.createElement('div');it.className='dd-item';it.textContent=m[1];
it.onclick=function(){pick(m[1]);dd.classList.remove('open');};
ls.appendChild(it);});});
bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};
ls.onclick=function(e){e.stopPropagation();};
document.addEventListener('click',function(){dd.classList.remove('open');});})();
const ST=[['0','未声明'],['1','地面'],['2','空中'],['3','紧急'],['4','识别故障']];
function statusSync(){const v=String(fld('status').value);let lb='';
ST.forEach(function(u){if(u[0]===v)lb=u[1];});
const t=fld('dd3txt');
if(lb){t.textContent=lb;t.classList.remove('ph');}
else{t.textContent='-- 选择状态 --';t.classList.add('ph');}}
(function(){const dd=fld('dd3'),bt=fld('dd3btn'),ls=fld('dd3list');
ST.forEach(function(u){const it=document.createElement('div');it.className='dd-item';
it.textContent=u[1];
it.onclick=function(){fld('status').value=u[0];statusSync();dd.classList.remove('open');};
ls.appendChild(it);});
bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};
ls.onclick=function(e){e.stopPropagation();};
document.addEventListener('click',function(){dd.classList.remove('open');});})();
const UAT=[['0','未声明 (None)'],['1','固定翼 (Aeroplane)'],['2','多旋翼/直升机 (Multirotor)'],
['3','旋翼机 (Gyroplane)'],['4','垂直起降固定翼 (VTOL)'],['5','扑翼机 (Ornithopter)'],
['6','滑翔机 (Glider)'],['7','风筝 (Kite)'],['8','自由气球 (Free Balloon)'],
['9','系留气球 (Captive Balloon)'],['10','飞艇 (Airship)'],['11','降落伞 (Free Fall/Parachute)'],
['12','火箭 (Rocket)'],['13','系留动力飞行器 (Tethered)'],['14','地面障碍物 (Ground Obstacle)'],
['15','其他 (Other)']];
function uatSync(){const v=String(fld('ua_type').value);let lb='';
UAT.forEach(function(u){if(u[0]===v)lb=u[1];});
const t=fld('dd2txt');
if(lb){t.textContent=v+' - '+lb;t.classList.remove('ph');}
else{t.textContent='-- 选择机型代码 --';t.classList.add('ph');}}
(function(){const dd=fld('dd2'),bt=fld('dd2btn'),ls=fld('dd2list');
UAT.forEach(function(u){const it=document.createElement('div');it.className='dd-item';
it.textContent=u[0]+' - '+u[1];
it.onclick=function(){fld('ua_type').value=u[0];uatSync();dd.classList.remove('open');};
ls.appendChild(it);});
bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};
ls.onclick=function(e){e.stopPropagation();};
document.addEventListener('click',function(){dd.classList.remove('open');});})();
const GBC=[['0','微型'],['1','轻型'],['2','小型'],['3','中型'],['4','大型']];
function gbcSync(){const v=String(fld('ua_category_gb').value);let lb='';
GBC.forEach(function(u){if(u[0]===v)lb=u[1];});
const t=fld('dd4txt');
if(lb){t.textContent=v+' - '+lb;t.classList.remove('ph');}
else{t.textContent='-- 选择分类 --';t.classList.add('ph');}}
(function(){const dd=fld('dd4'),bt=fld('dd4btn'),ls=fld('dd4list');
GBC.forEach(function(u){const it=document.createElement('div');it.className='dd-item';
it.textContent=u[0]+' - '+u[1];
it.onclick=function(){fld('ua_category_gb').value=u[0];gbcSync();dd.classList.remove('open');};
ls.appendChild(it);});
bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};
ls.onclick=function(e){e.stopPropagation();};
document.addEventListener('click',function(){dd.classList.remove('open');});})();
async function load(){try{const r=await fetch('/api/config');const j=await r.json();
F.forEach(k=>{if(j[k]!==undefined&&j[k]!==null)fld(k).value=j[k];});
LASTCFG=j;
document.querySelectorAll('input[name=band]').forEach(function(r){r.checked=(+r.value===(+j.band||0));});
if(!j.has5g){const b5=document.getElementById('b5gl');if(b5)b5.style.display='none';}
setRole((+j.band===1)?!!j.tx_5g:!!j.tx_2g);
document.querySelectorAll('input[name=std]').forEach(function(r){r.checked=(+r.value===j.rid_standard);});
uatSync();statusSync();gbcSync();bandUI();modeUI();stdUI();
if(roleTx()&&typeof initMap==='function')initMap();}
catch(e){msg('加载失败: '+e,false);}}
const NUM=['channel','beacon_interval_ms','ua_type','base_latitude','base_longitude',
'base_altitude_msl','operator_latitude','operator_longitude','operator_altitude',
'altitude_agl','speed_horizontal','status',
'patrol_speed','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];
async function save(){const d={};F.forEach(function(k){
const v=fld(k).value;
d[k]=NUM.indexOf(k)>=0?(v===''?null:parseFloat(v)):v;});
const _b=+getRadio('band'),_L=LASTCFG||{};
const _tx=roleTx()?1:0,_rx=_tx?0:1;
d.band=_b;d.rid_standard=+getRadio('std');
d.tx_2g=_b===0?_tx:(_L.tx_2g!==undefined?_L.tx_2g:1);
d.rx_2g=_b===0?_rx:(_L.rx_2g!==undefined?_L.rx_2g:0);
d.tx_5g=_b===1?_tx:(_L.tx_5g!==undefined?_L.tx_5g:1);
d.rx_5g=_b===1?_rx:(_L.rx_5g!==undefined?_L.rx_5g:0);
d.channel_5g=149;
if(_b===1)d.channel=(_L.channel||6);
try{const r=await fetch('/api/config',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});
const j=await r.json();
if(j.ok){msg('已保存并应用 ✓'+(j.restarted?' (SSID/信道/广播间隔变更会重建热点，连接可能短暂中断，请稍候或重新连接)':''),true);
try{if(map&&circle){map.removeLayer(circle);circle=null;}
if(typeof drawCircle==='function')drawCircle();}catch(me){}}
else{msg('保存失败: '+(j.err||''),false);}}
catch(e){msg(e&&e.name==='TypeError'?'热点参数已生效，连接被重置，请重新连接热点后刷新确认':'保存失败: '+e,false);}}
async function resetCfg(){if(!confirm('确认恢复全部初始设定？当前配置将被覆盖。'))return;
try{const r=await fetch('/api/reset',{method:'POST'});const j=await r.json();
if(j.ok){msg('已恢复初始设定 ✓'+(j.restarted?' (热点参数变化，请重新连接热点)':''),true);
await load();}else{msg('恢复失败',false);}}
catch(e){msg(e&&e.name==='TypeError'?'已恢复初始设定，连接被重置，请重新连接热点后刷新确认':'恢复失败: '+e,false);}}
function getRadio(n){const r=document.querySelector('input[name='+n+']:checked');
return r?r.value:'0';}
function stdUI(){const s=getRadio('std');
const oldOn=(s!=='1'),newOn=(s!=='0');
document.querySelectorAll('.oldstd').forEach(function(e){e.style.display=oldOn?'block':'none';});
document.querySelectorAll('.newstd').forEach(function(e){e.style.display=newOn?'block':'none';});}
document.querySelectorAll('input[name=std]').forEach(function(r){
r.addEventListener('change',stdUI);});
let rxTimer=null;let LASTCFG={};
function roleTx(){const r=document.querySelector('input[name=role]:checked');
return r?r.value==='tx':true;}
function setRole(tx){document.querySelectorAll('input[name=role]').forEach(function(r){
r.checked=(r.value===(tx?'tx':'rx'));});}
function bandUI(){const b=getRadio('band');const c=fld('channel');if(!c)return;
const lb=document.getElementById('chlbl');
if(b==='1'){c.value=149;c.disabled=true;
if(lb)lb.textContent='Wi-Fi 信道 (149-165)';}else{c.disabled=false;
if(lb)lb.textContent='Wi-Fi 信道 (1-13)';
if(+c.value>13)c.value=(LASTCFG&&LASTCFG.channel)||6;}}
function modeUI(){const tx=roleTx();const rx=!tx;
document.getElementById('stdbox').style.display=tx?'block':'none';
document.querySelectorAll('.txonly').forEach(function(e){e.style.display=tx?'':'none';});
document.getElementById('rxbox').style.display=rx?'block':'none';
if(rx&&!rxTimer){pollRx();rxTimer=setInterval(pollRx,2000);}
if(!rx&&rxTimer){clearInterval(rxTimer);rxTimer=null;}
if(rx){rxMapLoad();try{if(window.RXMAP)RXMAP.resize();}catch(e){}}
if(tx&&!ginit&&typeof initMap==='function')initMap();}
document.querySelectorAll('input[name=band]').forEach(function(r){
r.addEventListener('change',bandUI);});
document.querySelectorAll('input[name=role]').forEach(function(r){
r.addEventListener('change',modeUI);});
let _rxmAsked=false;
function rxMapFail(msg){try{
const b=q('rxmapbox');if(b)b.style.display='none';
let n=q('rxmapfail');
if(!n){n=document.createElement('div');n.className='note';n.id='rxmapfail';
const host=q('rxbox');if(host&&q('rxlist'))host.insertBefore(n,q('rxlist'));}
if(n)n.textContent=msg;}catch(e){}}
function rxMapLoad(){
if(_rxmAsked||window.RXMAP)return;_rxmAsked=true;
const s=document.createElement('script');s.src='/rxmap.js?v=1';
s.onload=function(){try{if(window.RXMAP)RXMAP.boot();}
catch(e){rxMapFail('地图初始化失败，已降级为纯列表模式');}};
s.onerror=function(){_rxmAsked=false;
rxMapFail('地图脚本未加载（需联网），已降级为纯列表模式');};
document.head.appendChild(s);}
const RXST=['未报告','地面','空中','紧急','识别失效','识别失效'];
const UATN=['未声明','固定翼','多旋翼','旋翼机','垂直起降固定翼','扑翼机','滑翔机','风筝',
'自由气球','系留气球','飞艇','降落伞','火箭','系留动力飞行器','地面障碍物','其他'];
const GBCN=['微型','轻型','小型','中型','大型'];
function q(id){return document.getElementById(id);}
function esc(s){return String(s===undefined||s===null?'':s)
.replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function f1(v){return (v===null||v===undefined||isNaN(v))?'--':Number(v).toFixed(1);}
function f6(v){return (v===null||v===undefined||isNaN(v))?'--':Number(v).toFixed(6);}
function ageTxt(ms){const s=Math.floor(ms/1000);
return s<60?(s+' 秒前'):(s<3600?(Math.floor(s/60)+' 分前'):(Math.floor(s/3600)+' 时前'));}
function rxProto(p){
if(p===4)return {c:'g46750',t:'GB46750',col:'#5b21b6'};
if(p===3)return {c:'g42590',t:'GB42590',col:'#0369a1'};
return {c:'astm',t:'ASTM',col:'#64748b'};}
function sigBar(r){
const lv=r>=-55?4:(r>=-67?3:(r>=-78?2:(r>=-90?1:0)));
const cls=lv>=3?'':(lv===2?'w':'weak');
let h=`<div class="sig" title="${r} dBm">`;
for(let i=1;i<=4;i++){const on=i<=lv;
h+=`<i class="${on?'on '+(lv>0?cls:''):''}" style="height:${4+i*2.5}px"></i>`;}
return h+'</div>';}
function cell(v,unit,label,cls){
return `<div class="${cls||''}"><b>${v}${unit?`<small>${unit}</small>`:''}</b><span>${label}</span></div>`;}
function posOk(la,lo){la=Number(la);lo=Number(lo);
return isFinite(la)&&isFinite(lo)&&Math.abs(la)<=90&&Math.abs(lo)<=180
&&!(Math.abs(la)<1e-6&&Math.abs(lo)<1e-6);}
function isIOS(){return /iPad|iPhone|iPod/.test(navigator.userAgent||'');}
function navDestName(kind,nm){
const tag=(kind==='op')?'飞手':'无人机';
const base=(nm?nm:tag);
return base.slice(0,40);}
function navScheme(kind,la,lo,nm,prov,alt){
la=Number(la);lo=Number(lo);
const dn=encodeURIComponent(navDestName(kind,nm));
if(prov==='bd'){
const dest='latlng:'+la.toFixed(6)+','+lo.toFixed(6)+'|name:'+dn;
const ios=isIOS();
const head=alt?(ios?'bdapp://map/direction?':'baidumap://map/direction?')
:(ios?'baidumap://map/direction?':'bdapp://map/direction?');
return head+'destination='+dest+
'&mode=driving&coord_type=wgs84&src=webapp.crid.nav';}
return (isIOS()?'iosamap://path?':'amapuri://route/plan/?')
+'sourceApplication=crid&dlat='+la.toFixed(6)+'&dlon='+lo.toFixed(6)
+'&dname='+dn+'&dev=1&t=0';}
function navSchemes(kind,la,lo,nm,prov){
const a=navScheme(kind,la,lo,nm,prov,0);
if(prov==='bd')return [a,navScheme(kind,la,lo,nm,prov,1)];
return [a];}
function navUrl(kind,la,lo,nm,prov){
la=Number(la);lo=Number(lo);
const name=navDestName(kind,nm);
if(prov==='bd'){
return 'https://api.map.baidu.com/mobile/direction?destination=latlng:'
+la.toFixed(6)+','+lo.toFixed(6)+'|name:'+encodeURIComponent(name)
+'&mode=driving&output=html&coord_type=wgs84&src=webapp.crid.nav';}
return 'https://uri.amap.com/navigation?to='+lo.toFixed(6)+','+la.toFixed(6)+','
+encodeURIComponent(name)+'&mode=car&policy=1&coordinate=wgs84&src=crid';}
function navTo(kind,la,lo,nm,prov){
if(!posOk(la,lo)){msg('该目标暂未上报有效坐标，无法导航',false);return;}
const list=navSchemes(kind,la,lo,nm,prov);
const web=navUrl(kind,la,lo,nm,prov);
let i=0,jumped=false,tm=null;
const stop=function(){jumped=true;if(tm){clearTimeout(tm);tm=null;}
document.removeEventListener('visibilitychange',stop);
window.removeEventListener('blur',stop);
window.removeEventListener('pagehide',stop);};
document.addEventListener('visibilitychange',stop);
window.addEventListener('blur',stop);
window.addEventListener('pagehide',stop);
const next=function(){
if(jumped)return;
if(i>=list.length){
if(document.visibilityState!=='hidden'){try{location.href=web;}catch(e){}}
return;}
try{location.href=list[i++];}catch(e){}
tm=setTimeout(next,900);};
next();}
const NP={kind:'drone',la:0,lo:0,nm:''};
function navClose(){const m=q('navpick');if(m)m.classList.remove('on');}
function navPick(kind,la,lo,nm){
if(!posOk(la,lo)){msg('该目标暂未上报有效坐标，无法导航',false);return;}
NP.kind=kind;NP.la=Number(la);NP.lo=Number(lo);NP.nm=nm||'';
const t=q('navpick_t');if(t)t.textContent='导航到'+((kind==='op')?'飞手位置':'无人机位置');
const p=q('navpick_p');
if(p)p.textContent=navDestName(kind,nm)+' · '+NP.la.toFixed(6)+', '+NP.lo.toFixed(6)+'（WGS-84）';
const m=q('navpick');if(m)m.classList.add('on');}
function navGo(prov){navClose();
navTo(NP.kind,NP.la,NP.lo,NP.nm,prov);}
function navBtn(kind,la,lo,nm){
const tag=(kind==='op')?'飞手':'无人机';
return '<button type="button" class="navbtn" data-nav="'+kind+'"'
+' data-la="'+Number(la).toFixed(6)+'" data-lo="'+Number(lo).toFixed(6)+'"'
+' data-nm="'+esc(nm||'')+'">导航到'+tag+'</button>';}
const OPT=['实时位置','起飞点位置','自定义位置','未知'];
const AGLT=['起飞点','地面','自定义','实时 GNSS'];
const HACC=['未知','<18.5 km','<7.4 km','<3.7 km','<1.85 km','<926 m','<556 m','<185 m',
'<93 m','<30 m','<10 m','<3 m','<1 m'];
const VACC=['未知','<150 m','<45 m','<25 m','<10 m','<3 m','<1 m'];
const SACC=['未知','<10 m/s','<3 m/s','<1 m/s','<0.3 m/s'];
const TACC=['未知','<=0.2 s','<=0.5 s','<=1 s','<=2 s','<=5 s','<=10 s','<=20 s',
'<=30 s','<=60 s','>60 s'];
const s_fold={};
function updFoldBtn(){const b=q('foldall');if(!b)return;
const cs=document.querySelectorAll('#rxlist .dcard');
const allOpen=cs.length>0&&Array.prototype.every.call(cs,function(c){return c.classList.contains('open');});
b.textContent=allOpen?'全部折叠 ▴':'全部展开 ▾';}
function foldAll(){const cs=document.querySelectorAll('#rxlist .dcard');
const anyClosed=Array.prototype.some.call(cs,function(c){return !c.classList.contains('open');});
Array.prototype.forEach.call(cs,function(c){c.classList.toggle('open',anyClosed);
s_fold[c.dataset.mac]=anyClosed;});updFoldBtn();}
function dtlHtml(t){
const p=rxProto(t.protocol);
const rows=[];const row=function(k,v){if(v!==undefined&&v!==null&&v!=='')rows.push(
'<tr><td>'+k+'</td><td>'+v+'</td></tr>');};
const txt=function(k,v){row(k,(v===undefined||v===null||v==='')?undefined:esc(v));};
txt('MAC 地址',t.mac);
txt('UAS ID (序列号)',t.uas_id);
txt('机型 / 描述',t.drone_name);
row('标准 / 协议',p.t+' · 协议号 '+t.protocol);
row('机型分类',t.protocol===4
?('新国标分类 '+t.ua_type+' · '+(GBCN[t.ua_type]||'未定义'))
:('机型代码 '+t.ua_type+' · '+(UATN[t.ua_type]||'未定义')));
txt('第二识别码',t.uas_id2);
txt('操作员 ID',t.operator_id);
txt('实名登记号',t.realname_id);
row('运行状态',RXST[t.status]||('状态 '+t.status));
row('纬度',f6(t.latitude));
row('经度',f6(t.longitude));
row('大地高度 (013)',f1(t.altitude_geo)+' m');
row('相对高度 (011)',t.altitude_agl===undefined?undefined:
f1(t.altitude_agl)+' m'+(t.agl_type!==undefined?' · '+AGLT[t.agl_type]:''));
row('气压高度 (014)',t.altitude_baro===undefined?undefined:f1(t.altitude_baro)+' m');
row('水平速度',f1(t.speed_horizontal)+' m/s');
row('垂直速度 (012)',f1(t.speed_vertical)+' m/s');
row('航向',f1(t.direction)+' °');
if(t.has_op_loc){
row('飞手纬度',f6(t.op_latitude));
row('飞手经度',f6(t.op_longitude));
row('飞手海拔 (007)',f1(t.op_altitude_geo)+' m');
}
if(t.protocol===4){
row('遥控站位置类型 (005)',OPT[t.op_loc_type!==undefined?t.op_loc_type:3]);
row('坐标系 (016)',t.coord_sys===undefined?undefined:
(t.coord_sys===0?'WGS-84':'类型 '+t.coord_sys));
row('水平精度 (017)',t.h_acc===undefined?undefined:(HACC[t.h_acc]||('枚举 '+t.h_acc)));
row('垂直精度 (018)',t.v_acc===undefined?undefined:(VACC[t.v_acc]||('枚举 '+t.v_acc)));
row('速度精度 (019)',t.spd_acc===undefined?undefined:(SACC[t.spd_acc]||('枚举 '+t.spd_acc)));
row('报文时间 (020)',t.ts_unix_ms===undefined?undefined:
new Date(t.ts_unix_ms).toLocaleTimeString());
row('时间戳精度 (021)',t.ts_acc===undefined?undefined:(TACC[t.ts_acc]||('枚举 '+t.ts_acc)));
}else{
row('报文时间 (相对)',t.ts_rel_10s===undefined?undefined:f1(t.ts_rel_10s/10)+' s');
row('水平精度',t.h_acc===undefined?undefined:(HACC[t.h_acc]||('枚举 '+t.h_acc)));
row('垂直精度',t.v_acc===undefined?undefined:(VACC[t.v_acc]||('枚举 '+t.v_acc)));
row('速度精度',t.spd_acc===undefined?undefined:(SACC[t.spd_acc]||('枚举 '+t.spd_acc)));
row('高度基准',t.agl_type===undefined?undefined:(AGLT[t.agl_type]||('类型 '+t.agl_type)));
}
row('信道',t.channel);
row('信号强度 (RSSI)',t.rssi===undefined?undefined:t.rssi+' dBm');
row('累计帧数',t.msg_count===undefined?undefined:t.msg_count+' 帧');
row('最后收到',t.age_ms===undefined?undefined:ageTxt(t.age_ms));
row('在线状态',t.active===false?'已离线（10 秒无信号，保留 30 分钟）':'在线');
row('已观测时长',t.first_age_ms===undefined?undefined:ageTxt(t.first_age_ms));
if(!rows.length)return '';
return '<div class="dtl"><div class="dtl-h">报文详情 ('+rows.length+' 项)<i>▶</i></div>'
+'<div class="dtl-b"><table class="dtab">'+rows.join('')+'</table></div></div>';}
function rxCard(t){
const p=rxProto(t.protocol);
const el=document.createElement('div');
el.className='dcard'+(t.active===false?' off':'');
el.dataset.mac=t.mac;
const stTxt=RXST[t.status]||('状态'+t.status);
const air=(t.status===2);
let name=t.drone_name||'';
if(!name)name=(t.protocol===4)?('新国标 '+(GBCN[t.ua_type]||('分类'+t.ua_type))):'未广播机型名';
const lnk=esc(t.mac)+' · '+t.rssi+' dBm · '+t.msg_count+' 帧 · '+ageTxt(t.age_ms);
let h=`<div class="dh"><span class="id" title="${esc(t.uas_id)}">${esc(t.uas_id||'(无 UAS ID)')}</span>`
+`<span class="tag ${p.c}">${p.t}</span>`
+`<span class="tag ${air?'air':'pend'}">${esc(stTxt)}</span>`
+sigBar(t.rssi)+`</div><div class="db">`;
h+=`<div class="dname"><span class="nm" title="${esc(name)}">${esc(name)}</span>`
+`<span class="lnk" title="链路与统计：MAC · 信号强度 · 累计帧数 · 最后收到">${lnk}</span></div>`;
h+=`<div class="kv">`
+cell(f6(t.latitude),'','纬度','hl')
+cell(f6(t.longitude),'','经度','hl')
+cell(f1(t.altitude_geo),'m','大地高度')
+cell(f1(t.speed_horizontal),'m/s','水平速度')
+cell(f1(t.direction),'°','航向')
+cell(f1(t.speed_vertical),'m/s','垂直速度')
+`</div>`;
const idt=[];
if(t.has_op_loc)idt.push([f6(t.op_latitude)+', '+f6(t.op_longitude),'飞手位置 (遥控站)']);
if(t.realname_id)idt.push([esc(t.realname_id),'实名登记号']);
if(idt.length){h+='<div class="kv2">';
idt.forEach(function(x){
h+='<div'+(idt.length===1?' class="wide"':'')+'><b>'+x[0]+'</b><span>'+x[1]+'</span></div>';});
h+='</div>';}
let nb='';
if(posOk(t.latitude,t.longitude))nb+=navBtn('drone',t.latitude,t.longitude,t.drone_name);
if(t.has_op_loc&&posOk(t.op_latitude,t.op_longitude))
nb+=navBtn('op',t.op_latitude,t.op_longitude,'');
if(nb)h+='<div class="navrow">'+nb+'</div>';
h+=dtlHtml(t)+'</div>';
el.innerHTML=h;
const dh=el.querySelector('.dtl-h');
if(dh){dh.onclick=function(ev){ev.stopPropagation();
el.classList.toggle('open');s_fold[t.mac]=el.classList.contains('open');updFoldBtn();};}
Array.prototype.forEach.call(el.querySelectorAll('.navbtn'),function(b){
b.onclick=function(ev){ev.stopPropagation();
navPick(b.dataset.nav,b.dataset.la,b.dataset.lo,b.dataset.nm);};});
el.onclick=function(){try{if(window.RXMAP)RXMAP.focus(t.mac);}catch(e){}};
return el;}

async function pollRx(){
try{
const r=await fetch('/api/rx');const j=await r.json();
try{if(window.RXMAP)RXMAP.update(j.tracks);}catch(e){}
const s=j.stats;
const on=[],off=[];
j.tracks.forEach(function(t){(t.active===false?off:on).push(t);});
on.sort(function(a,b){return b.rssi-a.rssi;});
off.sort(function(a,b){return a.age_ms-b.age_ms;});
q('s_online').textContent=on.length;
q('s_rid').textContent=s.rid;
q('s_total').textContent=s.total;
q('s_ok').textContent=s.ok;
q('s_ovf').textContent=s.ovf;
q('s_ovfbox').className='chip'+((s.ovf>0)?' warn':'');
let fresh=0;
on.forEach(function(t){if(t.age_ms<3000)fresh++;});
q('rxstate').innerHTML=(fresh>0?`<b style="color:#0a7d43">${fresh} 个目标</b>信号良好（3 秒内）`
:'暂未收到 RID 帧，持续监听中…')+` &nbsp;|&nbsp; 更新于 ${new Date().toLocaleTimeString()}`;
const box=q('rxlist');
box.innerHTML='';
if(!on.length&&!off.length){
box.innerHTML='<div class="rxb0"><b>◎</b>尚未发现无人机 RID 广播<br>周边设备需与本机处于同一 Wi-Fi 信道</div>';
return;}
function sec(txt,dim){const d=document.createElement('div');
d.className='rxsec'+(dim?' dim':'');d.textContent=txt;box.appendChild(d);}
if(on.length){sec('在线信号 · '+on.length,false);
on.forEach(function(t){box.appendChild(rxCard(t));});}
if(off.length){sec('已离线 · '+off.length+'（10 秒无信号，保留 30 分钟）',true);
off.forEach(function(t){box.appendChild(rxCard(t));});}
Array.prototype.forEach.call(box.querySelectorAll('.dcard'),function(c){
if(s_fold[c.dataset.mac])c.classList.add('open');});
updFoldBtn();
}catch(e){q('rxstate').textContent='轮询失败：'+e;}}
q('foldall').onclick=foldAll;
(function(){
const m=q('navpick');if(!m)return;
m.onclick=function(ev){if(ev.target===m)navClose();};
Array.prototype.forEach.call(m.querySelectorAll('.navopt'),function(b){
b.onclick=function(ev){ev.stopPropagation();navGo(b.getAttribute('data-prov'));};});
const c=q('navpick_c');if(c)c.onclick=navClose;
})();
let gbase=[0,0],gpick=null;
function dotIcon(c){return L.divIcon({className:'',
html:'<div style="width:14px;height:14px;border-radius:50%;background:'+c+
';border:2px solid #fff;box-shadow:0 1px 3px rgba(0,0,0,.4)"></div>',
iconSize:[14,14],iconAnchor:[7,7]});}
function drawCircle(){if(!map)return;if(circle){map.removeLayer(circle);circle=null;}
const r=(parseFloat(fld('patrol_radius_lat').value)||0)*111320;
if(r>0&&dMarker){circle=L.circle(dMarker.getLatLng(),{radius:r,color:'#0a7d43',
weight:1,fillOpacity:.05}).addTo(map);}}
function initMap(){
if(ginit){syncMarkers();return;}
ginit=true;const o=opLL(),d=baseLL();
if(typeof L!=='undefined'&&!window._mapfail){
map=L.map('map').setView(o,15);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',
{maxZoom:19,attribution:'&copy; OpenStreetMap'}).addTo(map);
opMarker=L.marker(o,{draggable:true,icon:dotIcon('#c0392b')}).addTo(map);
dMarker=L.marker(d,{draggable:true,icon:dotIcon('#0a7d43')}).addTo(map);
opMarker.on('dragend',function(e){const p=opMarker.getLatLng();
applyPick('op',p.lat,p.lng);});
dMarker.on('dragend',function(e){const p=dMarker.getLatLng();
applyPick('drone',p.lat,p.lng);});
map.on('click',function(e){applyPick(ptarget(),e.latlng.lat,e.latlng.lng);});
live=L.circleMarker(d,{radius:6,color:'#1565c0',fillColor:'#1565c0',
fillOpacity:.9}).addTo(map);
drawCircle();}
else{document.getElementById('map').style.display='none';
document.getElementById('gridbox').style.display='block';
gbase=o;drawGrid();
document.getElementById('mpp').onchange=function(){drawGrid();};
const cv=document.getElementById('grid');
cv.addEventListener('click',function(e){
const rc=cv.getBoundingClientRect();const s=320/rc.width;
const x=(e.clientX-rc.left)*s,y=(e.clientY-rc.top)*s;
const mpp=parseFloat(fld('mpp').value);
const dx=(x-160)*mpp,dy=(160-y)*mpp;
const la=gbase[0]+dy/111320;
const lo=gbase[1]+dx/(111320*Math.cos(gbase[0]*Math.PI/180));
applyPick(ptarget(),la,lo);gpick=[la,lo];drawGrid();});}}
function drawGrid(){const cv=document.getElementById('grid');
if(!cv)return;
const x=cv.getContext('2d');
x.fillStyle='#fafafa';x.fillRect(0,0,320,320);
x.strokeStyle='#e2e2e2';
for(let i=0;i<=320;i+=40){x.beginPath();x.moveTo(i,0);x.lineTo(i,320);x.stroke();
x.beginPath();x.moveTo(0,i);x.lineTo(320,i);x.stroke();}
x.strokeStyle='#999';x.beginPath();x.moveTo(160,0);x.lineTo(160,320);
x.moveTo(0,160);x.lineTo(320,160);x.stroke();
x.fillStyle='#555';x.font='11px sans-serif';
x.fillText(fld('mpp').value+'米/像素',8,312);
x.fillStyle='#0a7d43';x.beginPath();x.arc(160,160,6,0,6.3);x.fill();
x.fillStyle='#0a7d43';x.fillText('中心=基准点',170,164);
if(trailPts.length>1){const tm=parseFloat(fld('mpp').value);
x.strokeStyle='rgba(21,101,192,.55)';x.lineWidth=2;x.beginPath();
trailPts.forEach(function(p,i){
const px=160+(p[1]-gbase[1])*111320*Math.cos(gbase[0]*Math.PI/180)/tm;
const py=160-(p[0]-gbase[0])*111320/tm;
if(i)x.lineTo(px,py);else x.moveTo(px,py);});x.stroke();x.lineWidth=1;}
if(gpick){const mpp=parseFloat(fld('mpp').value);
const px=160+(gpick[1]-gbase[1])*111320*Math.cos(gbase[0]*Math.PI/180)/mpp;
const py=160-(gpick[0]-gbase[0])*111320/mpp;
x.fillStyle='#c0392b';x.beginPath();x.arc(px,py,6,0,6.3);x.fill();}}
function trailPush(la,lo){const n=trailPts.length;
if(n){const dla=la-trailPts[n-1][0],dlo=lo-trailPts[n-1][1];
if(Math.abs(dla)>0.005||Math.abs(dlo)>0.005){trailPts=[];}
else if(Math.abs(dla)>1e-7||Math.abs(dlo)>1e-7)trailPts.push([la,lo]);}
else{trailPts.push([la,lo]);}
if(trailPts.length>240)trailPts.shift();
if(map){if(!trailLine)trailLine=L.polyline(trailPts,
{color:'#1565c0',weight:2,opacity:.6}).addTo(map);
else trailLine.setLatLngs(trailPts);}}
async function poll(){try{const r=await fetch('/api/status');const j=await r.json();
const t='实时广播位置: '+j.latitude.toFixed(6)+', '+j.longitude.toFixed(6)+
'  高度 '+j.altitude_msl.toFixed(1)+'m  航向 '+j.heading.toFixed(0)+'°';
document.getElementById('liveinfo').textContent=t;
if(live){live.setLatLng([j.latitude,j.longitude]);}
trailPush(j.latitude,j.longitude);}
catch(e){}
if(Date.now()-sT>600000)syncTime();}
var sT=0;
function syncTime(){sT=Date.now();try{fetch('/api/time',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch_ms:sT})})
.catch(function(){});}catch(e){}}
setInterval(poll,2000);
syncTime();
load();
