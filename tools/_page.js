
let map=null,opMarker=null,dMarker=null,live=null,circle=null,ginit=false;
let trailPts=[],trailLine=null;
const F=['uas_id','drone_name','operator_id','realname_id','ssid','ap_password','channel','beacon_interval_ms','ua_type',
'base_latitude','base_longitude','base_altitude_msl','ground_altitude','altitude_agl','baro_altitude',
'speed_horizontal','heading','heading_mode','speed_vertical','vspd_mode',
'operator_latitude','operator_longitude','operator_altitude',
'status','height_type','operator_location_type',
'patrol_speed','altitude_wave','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];
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
function patrolCalc(){
const R=111320,la=parseFloat(fld('base_latitude').value)||0;
const rl=parseFloat(fld('patrol_radius_lat').value)||0;
const ro=parseFloat(fld('patrol_radius_lon').value)||0;
const om=(parseFloat(fld('patrol_speed').value)||0)/10;
const vy=rl*R*om,vx=ro*R*Math.cos(la*Math.PI/180)*om;
const v=Math.sqrt(vx*vx+vy*vy);
const rep=parseFloat(fld('speed_horizontal').value);
const wv=parseFloat(fld('altitude_wave').value);
const hm=String(fld('heading_mode')?fld('heading_mode').value:'0');
const vm=String(fld('vspd_mode')?fld('vspd_mode').value:'0');
const hd=parseFloat(fld('heading')?fld('heading').value:'');
const vv=parseFloat(fld('speed_vertical')?fld('speed_vertical').value:'');
const el=fld('patrolCalc');if(!el)return;
let s='轨迹真实线速度约 '+v.toFixed(2)+' m/s；报文上报的「水平速度」为 '
+(isNaN(rep)?'--':rep.toFixed(1))+' m/s（由飞行参数里的输入决定，巡游不会改写它）';
s+='。两者差距过大时，接收端用坐标序列反算的速度会与报文对不上，容易被判为伪造';
if(vm==='1'){s+='。垂直速度固定为 '+(isNaN(vv)?'--':vv.toFixed(1))+' m/s，不随高度波动变化';}
else if(!isNaN(wv)){
if(wv>0)s+='。高度在基准值上下 ±'+wv.toFixed(1)+' m 摆动，约 10.5 分钟一轮，峰值垂直速度 '+
(wv*0.01).toFixed(2)+' m/s；填 0 可固定高度';
else s+='。高度波动幅度为 0：高度固定，垂直速度恒为 0';}
if(hm==='1')s+='。航向固定为 '+(isNaN(hd)?'--':hd.toFixed(1))+'°，不随轨迹切线变化';
else s+='。航向由轨迹切线自动推算';
el.textContent=s;}
['patrol_speed','patrol_radius_lat','patrol_radius_lon','base_latitude','speed_horizontal',
'altitude_wave','heading','heading_mode','speed_vertical','vspd_mode']
.forEach(function(id){const e=fld(id);if(e)e.addEventListener('input',patrolCalc);});
function bindFixVal(id,modeId){const e=fld(id),m=fld(modeId);
if(!e||!m)return;e.addEventListener('input',function(){
if(m.value!=='1'){m.value='1';ddSync(modeId);}
if(typeof patrolCalc==='function')patrolCalc();});}
bindFixVal('heading','heading_mode');bindFixVal('speed_vertical','vspd_mode');
let lastMsl=NaN;
function altSync(src){
const m=fld('base_altitude_msl'),g=fld('ground_altitude'),a=fld('altitude_agl');
const b=fld('baro_altitude');
const M=parseFloat(m.value),G=parseFloat(g.value),A=parseFloat(a.value);
const B=b?parseFloat(b.value):NaN;
if(src==='altitude_agl'){if(!isNaN(A)&&!isNaN(G))m.value=(A+G).toFixed(1);}
else{if(!isNaN(M)&&!isNaN(G))a.value=(M-G).toFixed(1);}
if(b&&src!=='load'){const nM=parseFloat(m.value);
if(!isNaN(nM)&&!isNaN(B)&&!isNaN(lastMsl)&&Math.abs(B-lastMsl)<0.05)b.value=nM.toFixed(1);
if(isNaN(B)||isNaN(lastMsl))b.value=nM.toFixed(1);}
lastMsl=parseFloat(m.value);
if(typeof patrolCalc==='function')patrolCalc();}
['base_altitude_msl','ground_altitude','altitude_agl'].forEach(function(id){
fld(id).addEventListener('input',function(){altSync(id);});});
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
var DDS={};
function ddMake(n,hid,items,ph){var dd=fld('dd'+n),bt=fld('dd'+n+'btn'),
ls=fld('dd'+n+'list'),tx=fld('dd'+n+'txt');
if(!dd||!bt||!ls||!tx)return;
function sync(){var v=String(fld(hid).value),lb='';
items.forEach(function(u){if(u[0]===v)lb=u[1];});
if(lb){tx.textContent=lb;tx.classList.remove('ph');}
else{tx.textContent=ph;tx.classList.add('ph');}}
items.forEach(function(u){var it=document.createElement('div');it.className='dd-item';
it.textContent=u[1];
it.onclick=function(){fld(hid).value=u[0];sync();dd.classList.remove('open');
if(typeof patrolCalc==='function')patrolCalc();};
ls.appendChild(it);});
bt.onclick=function(e){e.stopPropagation();dd.classList.toggle('open');};
ls.onclick=function(e){e.stopPropagation();};
document.addEventListener('click',function(){dd.classList.remove('open');});
DDS[hid]=sync;sync();}
function ddSync(id){if(DDS[id])DDS[id]();}
ddMake('5','heading_mode',[['0','自动（跟随巡游）'],['1','固定（用左侧值）']],'-- 选择来源 --');
ddMake('6','vspd_mode',[['0','自动（跟高度波动）'],['1','固定（用左侧值）']],'-- 选择来源 --');
ddMake('7','height_type',[['0','起飞点高度'],['1','地面高度']],'-- 选择高度基准 --');
ddMake('8','operator_location_type',[['0','起飞点'],['1','实时 GNSS'],['2','固定位置']],
'-- 选择位置类型 --');
async function load(){try{const r=await fetch('/api/config');const j=await r.json();
F.forEach(k=>{if(j[k]!==undefined&&j[k]!==null)fld(k).value=j[k];});
LASTCFG=j;
document.querySelectorAll('input[name=band]').forEach(function(r){r.checked=(+r.value===(+j.band||0));});
if(!j.has5g){var bb=document.getElementById('bandbox');if(bb)bb.style.display='none';
var bl=document.getElementById('bandln');if(bl)bl.style.display='none';}
setRole((+j.band===1)?!!j.tx_5g:!!j.tx_2g);
document.querySelectorAll('input[name=std]').forEach(function(r){r.checked=(+r.value===j.rid_standard);});
uatSync();statusSync();gbcSync();bandUI();modeUI();stdUI();
ddSync('heading_mode');ddSync('vspd_mode');ddSync('height_type');ddSync('operator_location_type');
if(typeof altSync==='function')altSync('load');
if(typeof patrolCalc==='function')patrolCalc();
if(roleTx()&&typeof initMap==='function')initMap();}
catch(e){msg('加载失败: '+e,false);}}
const NUM=['channel','beacon_interval_ms','ua_type','base_latitude','base_longitude',
'base_altitude_msl','ground_altitude','baro_altitude','operator_latitude','operator_longitude',
'operator_altitude','altitude_agl','speed_horizontal','speed_vertical','heading','heading_mode',
'vspd_mode','height_type','operator_location_type','status',
'patrol_speed','altitude_wave','patrol_radius_lat','patrol_radius_lon','ua_category_gb'];
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
d.channel_5g=_b===1?d.channel:(_L.channel_5g||149);
if(_b===1)d.channel=(_L.channel||6);
const pwChg=(d.ap_password||'')!==(_L.ap_password||'');
try{const r=await fetch('/api/config',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});
const j=await r.json();
if(j.ok){msg('已保存并应用 ✓'+(pwChg?' (热点密码已修改，请用新密码重新连接)':(j.restarted?' (SSID/信道/广播间隔变更会重建热点，连接可能短暂中断，请稍候或重新连接)':'')),true);
try{if(map&&circle){map.removeLayer(circle);circle=null;}
if(typeof drawCircle==='function')drawCircle();}catch(me){}}
else{msg('保存失败: '+(j.err||''),false);}}
catch(e){msg(e&&e.name==='TypeError'?'热点参数已生效，连接被重置，请重新连接热点后刷新确认':'保存失败: '+e,false);}}
async function resetCfg(){if(!confirm('确认恢复全部初始设定？当前配置将被覆盖。'))return;
try{const r=await fetch('/api/reset',{method:'POST'});const j=await r.json();
if(j.ok){msg('已恢复初始设定 ✓'+(j.restarted?' (热点参数变化，请重新连接热点)':''),true);
await load();}else{msg('恢复失败',false);}}
catch(e){msg(e&&e.name==='TypeError'?'已恢复初始设定，连接被重置，请重新连接热点后刷新确认':'恢复失败: '+e,false);}}
var OTA=null,otaBusy=false,otaPollN=0;
function otaOpen(){var m=q('otamask');if(!m)return;m.classList.add('on');otaLoad();}
function otaClose(){if(otaBusy)return;var m=q('otamask');if(m)m.classList.remove('on');}
function otaProg(p){var b=q('otabar'),f=q('otafill');if(!b||!f)return;
b.className='otabar on';
f.style.width=Math.max(0,Math.min(100,p))+'%';}
function otaTxt(t,cls){var e=q('otatxt');if(!e)return;
e.className='otatxt'+(cls?' '+cls:'');e.textContent=t;}
function otaFill(){if(!OTA)return;var v=q('otaver'),p=q('otapart'),c=q('otacan');
var hv=q('otavh');if(hv)hv.textContent='v'+(OTA.ver||'--');
if(v)v.textContent=OTA.ver||'--';
if(p)p.textContent=(OTA.part||'--')+(OTA.part_size?' / '+(OTA.part_size/1048576).toFixed(2)+' MB':'');
if(c)c.textContent=OTA.ota?(OTA.max/1048576).toFixed(2)+' MB':'不可用';}
function otaLoad(){return fetch('/api/ota/info',{cache:'no-store'})
.then(function(r){return r.json();})
.then(function(j){OTA=j;otaFill();})
.catch(function(){otaTxt('升级信息读取失败：页面与固件版本可能不匹配','bad');});}
function otaStart(){if(otaBusy)return;
var fi=q('otafile');var file=fi&&fi.files?fi.files[0]:null;
if(!file){otaTxt('请先选择固件文件 (.bin)','bad');return;}
if(!OTA||!OTA.token){otaTxt('升级信息未就绪，请刷新页面后重试','bad');return;}
if(file.size<32768){otaTxt('文件太小，不是有效固件','bad');return;}
if(OTA.max&&file.size>OTA.max){otaTxt('文件超过分区容量 '+(OTA.max/1048576).toFixed(2)+' MB','bad');return;}
if(!confirm('确认升级固件？\n\n文件：'+file.name+'\n大小：'+(file.size/1048576).toFixed(2)+' MB'
+'\n当前版本：'+(OTA.ver||'?')+'\n\n升级期间请勿断电，完成后设备会自动重启。'))return;
otaBusy=true;var btn=q('otabtn');if(btn)btn.disabled=true;
otaProg(0);otaTxt('正在上传，请勿关闭页面…','');
var x=new XMLHttpRequest();
x.open('POST','/api/ota/upload',true);
x.setRequestHeader('Content-Type','application/octet-stream');
x.setRequestHeader('X-OTA-Token',OTA.token);
x.timeout=180000;
x.upload.onprogress=function(e){if(!e.lengthComputable)return;
var pc=e.loaded/e.total*100;otaProg(pc);
otaTxt('已上传 '+(e.loaded/1024).toFixed(0)+' / '+(e.total/1024).toFixed(0)+' KB（'+pc.toFixed(0)+'%）');};
x.onload=function(){var j=null;try{j=JSON.parse(x.responseText);}catch(e){}
if(x.status===200&&j&&j.ok){otaProg(100);otaTxt('写入完成，设备正在重启…','good');otaWaitReboot();}
else{otaBusy=false;if(btn)btn.disabled=false;
otaTxt('升级失败：'+((j&&j.err)||('HTTP '+x.status))+'（旧固件不受影响）','bad');}};
x.onerror=function(){otaBusy=false;if(btn)btn.disabled=false;
otaTxt('网络中断，升级未完成；设备若已重启请刷新页面确认版本','bad');};
x.ontimeout=function(){otaBusy=false;if(btn)btn.disabled=false;otaTxt('上传超时，已取消','bad');};
x.send(file);}
function otaWaitReboot(){var t0=Date.now();
var iv=setInterval(function(){
if(Date.now()-t0>60000){clearInterval(iv);otaBusy=false;
var b=q('otabtn');if(b)b.disabled=false;
otaTxt('设备 60 秒内未响应，请手动刷新页面查看版本','bad');return;}
otaPollN++;
fetch('/api/ota/info?n='+otaPollN,{cache:'no-store'})
.then(function(r){return r.json();})
.then(function(j){clearInterval(iv);otaBusy=false;
var same=(j.ver===OTA.ver);
otaTxt(same?'已重启，版本仍为 '+j.ver+'（上传的可能是同版本固件，或已被回滚）'
:'已升级到 '+j.ver+'，正在刷新页面…',same?'bad':'good');
setTimeout(function(){location.reload();},1600);})
.catch(function(){});
},2000);}
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
if(b==='1'){c.disabled=false;c.min=149;c.max=165;c.step=4;
if(!(+c.value>=149&&+c.value<=165))c.value=(LASTCFG&&LASTCFG.channel_5g)||149;
if(lb)lb.textContent='Wi-Fi 信道 (149/153/157/161/165)';}
else{c.disabled=false;c.min=1;c.max=13;c.step=1;
if(!(+c.value>=1&&+c.value<=13))c.value=(LASTCFG&&LASTCFG.channel)||6;
if(lb)lb.textContent='Wi-Fi 信道 (1-13)';}}
function modeUI(){const tx=roleTx();const rx=!tx;
document.getElementById('stdbox').style.display=tx?'block':'none';
document.querySelectorAll('.txonly').forEach(function(e){e.style.display=tx?'':'none';});
document.getElementById('rxbox').style.display=rx?'block':'none';
const lg=document.getElementById('logbox');if(lg)lg.style.display=rx?'':'none';
if(tx&&typeof lgStop==='function')lgStop();
if(rx)logRefresh();
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
if(otaBusy)return;
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
async function poll(){if(otaBusy)return;try{const r=await fetch('/api/status');const j=await r.json();
const t='实时广播位置: '+j.latitude.toFixed(6)+', '+j.longitude.toFixed(6)+
'  高度 '+j.altitude_msl.toFixed(1)+'m  航向 '+j.heading.toFixed(0)+'°';
document.getElementById('liveinfo').textContent=t;
if(live){live.setLatLng([j.latitude,j.longitude]);}
trailPush(j.latitude,j.longitude);
tStat(!!j.tsync,j.tnow);tLast=j.tsync?1:0;}
catch(e){}
tAuto();}
var sT=0,tTries=0,tDone=false,tBad='',tNext=0,tLast=-1;
function tClock(sec){var d=new Date(sec*1000);
return pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds());}
function tStat(ok,sec){var e=document.getElementById('tstat');if(!e)return;
if(ok&&sec){tBad='';e.className='ok';
e.textContent='时间已校准 · 设备时钟 '+tClock(sec)+'（报文按此时间发送）';}
else if(tBad){e.className='bad';
e.textContent='授时失败 · '+tBad+'（报文时间按「未知」发送）';}
else{e.className='';e.textContent='时间未校准 · 报文时间字段按「未知」发送';}}
function tAuto(){if(tTries>0)return;if(Date.now()<tNext)return;
if(Date.now()-sT>600000){syncTime();return;}
if(tLast===0)syncTime();}
let lgMac='',lgPage=0,lgPts=[],lgPlay=-1,lgTimer=null,lgRate=1;
let lgOn=false;
function lgSw(on){lgOn=!!on;var b=document.getElementById('lgsw');if(!b)return;
b.textContent='记录：'+(lgOn?'开':'关');b.classList.toggle('on',lgOn);}
async function lgEnable(on){try{var r;
if(bleOn){r=await fetch('/api/log/enable?on='+(on?1:0));}
else{r=await fetch('/api/log/enable',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify({on:on?1:0})});}
const j=await r.json();if(j&&j.ok){lgSw(j.on);return true;}}catch(e){}return false;}
function logToggle(){const want=!lgOn;lgEnable(want).then(function(ok){if(!ok){lgSw(lgOn);
msg('记录开关失败',false);return;}msg(want?'轨迹记录已开启（已保存）':'轨迹记录已关闭（已保存）',true);
if(want)logRefresh();});}
let lgPath=[],lgPages=[],lgMatched=0,lgLoading=false;
let lgOpen='';
let lgHasMap=false;
const LG_PER=30,LG_MAXP=10;
function fmtKB(n){n=Number(n)||0;if(n>=1048576)return (n/1048576).toFixed(2)+' MB';
return (n/1024).toFixed(0)+' KB';}
function pad2(x){return String(x).padStart(2,'0');}
function fmtT(u,t){
if(u>0){const d=new Date(u*1000);
return d.getFullYear()+'-'+pad2(d.getMonth()+1)+'-'+pad2(d.getDate())+' '+
pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds());}
const s=Math.floor((t||0)/1000);
return '上电 '+pad2(Math.floor(s/3600))+':'+pad2(Math.floor(s%3600/60))+':'+pad2(s%60);}
function logFold(){
const b=q('lgbody'),h=q('lghd');if(!b)return;
const fold=(b.style.display!=='none');
b.style.display=fold?'none':'';
if(h)h.className='lghd'+(fold?'':' open');
const t=q('lgtog');if(t)t.textContent=fold?'展开':'收起';
if(!fold)lgResize();}
function logTargets(j){
const box=q('lgtargets');if(!box)return;
const ts=j.targets||[];
if(!ts.length){box.innerHTML='<div class="lgmore">还没有记录。切到接收模式并等周边无人机出现，这里就会列出目标。</div>';return;}
let h='';
for(let i=0;i<ts.length;i++){const t=ts[i];
const sel=(t.mac===lgMac)?' sel':'';
h+='<div class="lgrow'+sel+'" data-mac="'+t.mac+'">'
+'<span class="id">'+(t.uid?esc(t.uid):'(无 UAS ID)')+'</span>'
+'<span class="mono">'+String(t.mac).toUpperCase()+'</span>'
+'<span class="sp"></span>'
+'<span class="mini">'+t.count+' 条 · 最后 '+fmtT(t.newest_unix,t.newest_tick)+'</span>'
+'<button type="button" data-lg="'+t.mac+'">看轨迹</button>'
+'<button type="button" class="lgdel" data-del="'+t.mac+'">删除</button>'
+'</div>';}
box.innerHTML=h;
Array.prototype.forEach.call(box.querySelectorAll('button[data-lg]'),function(b){
b.onclick=function(){logPoints(b.dataset.lg);};});
Array.prototype.forEach.call(box.querySelectorAll('button[data-del]'),function(b){
b.onclick=function(){logDel(b.dataset.del);};});}
function lgMarkSel(){const box=q('lgtargets');if(!box)return;
Array.prototype.forEach.call(box.querySelectorAll('.lgrow'),function(r){
r.className='lgrow'+(r.getAttribute('data-mac')===lgMac?' sel':'');});}
async function logPoints(mac,page){
if(mac===lgOpen&&typeof page==='undefined'){logClose();return;}
const same=(mac===lgMac);
lgOpen=mac;lgMac=mac;
if(typeof page==='number')lgPage=page;else if(!same)lgPage=0;
lgStop();lgPlay=-1;lgPts=[];lgPath=[];lgPages=[];lgMatched=0;lgLoading=false;
const d=q('lgdet');if(d)d.style.display='';
const w=q('lgwho');if(w)w.textContent=String(mac).toUpperCase()+' · 航线回放';
lgMarkSel();
await lgFetch();}
function logClose(){
lgStop();lgOpen='';lgMac='';lgPts=[];lgPath=[];lgPages=[];lgMatched=0;lgLoading=false;lgPlay=-1;lgPage=0;lgHasMap=false;
try{if(window.RXMAP&&RXMAP.replayEnd)RXMAP.replayEnd();}catch(e){}
const d=q('lgdet');if(d)d.style.display='none';
const b=q('lgpoints');if(b)b.innerHTML='';
const p=q('lgpage');if(p)p.innerHTML='';
lgMarkSel();}
function lgRev(a){return (a||[]).slice().reverse();}
function lgPageCount(){
let n=Math.ceil(lgMatched/LG_PER);if(n<1)n=1;if(n>LG_MAXP)n=LG_MAXP;return n;}
function lgAllLoaded(){
const n=lgPageCount();
for(let p=0;p<n;p++){if(!lgPages[p])return false;}return true;}
function lgRebuildPath(){
const arr=[];
for(let p=lgPages.length-1;p>=0;p--){const a=lgPages[p];if(!a)continue;
for(let i=0;i<a.length;i++){a[i].pg=p;a[i].ri=i;arr.push(a[i]);}}
lgPath=arr;}
async function lgFetch(){
const box=q('lgpoints');
if(box)box.innerHTML='<div class="lgmore">读取中…</div>';
const who=lgMac;
try{
const r=await fetch('/api/log/points?limit='+LG_PER+'&offset='+(lgPage*LG_PER)
+'&mac='+encodeURIComponent(lgMac));
const j=await r.json();
if(who!==lgMac)return;
lgMatched=Number(j.matched||0)||0;
lgPages[lgPage]=lgRev(j.points);
lgPts=lgPages[lgPage];
lgPlay=-1;
lgRebuildPath();
lgRender();lgPager();lgNow();
lgPathDraw();
if(!lgAllLoaded())lgFill();
}catch(e){if(box)box.innerHTML='<div class="lgmore">读取失败</div>';}}
async function lgFill(){
if(lgLoading||!lgMac)return;
lgLoading=true;
const who=lgMac;
const n=lgPageCount();
lgNow();
for(let p=0;p<n;p++){
if(lgPages[p])continue;
try{
const r=await fetch('/api/log/points?limit='+LG_PER+'&offset='+(p*LG_PER)
+'&mac='+encodeURIComponent(who));
const j=await r.json();
if(who!==lgMac)return;
const a=lgRev(j.points);
if(!a.length)break;
lgPages[p]=a;
}catch(e){break;}
lgRebuildPath();lgNow();}
lgLoading=false;
if(who!==lgMac)return;
lgPager();lgNow();lgPathDraw();}
function lgRender(){
const box=q('lgpoints');if(!box)return;
if(!lgPts.length){box.innerHTML='<div class="lgmore">'
+(lgPage>0?'本页没有记录了，往回翻一页试试。':'该目标暂无记录。')+'</div>';return;}
let h='<div class="lgpt hd"><span>时间</span><span>纬度</span><span>经度</span><span>高度</span><span>速度</span><span>航向</span></div>';
for(let i=0;i<lgPts.length;i++){const p=lgPts[i];
const ts=fmtT(p.t,p.tick);
const dt=p.t>0?('<b class="dt">'+ts.slice(5,10)+' </b>'):'';
const tm=p.t>0?ts.slice(11):ts;
h+='<div class="lgpt" data-i="'+i+'">'
+'<span class="mono">'+dt+tm+'</span>'
+'<span class="mono" data-l="纬 ">'+Number(p.lat).toFixed(5)+'</span>'
+'<span class="mono" data-l="经 ">'+Number(p.lon).toFixed(5)+'</span>'
+'<span data-l="高 ">'+Math.round(p.alt)+' m</span>'
+'<span data-l="速 ">'+Number(p.spd_h).toFixed(1)+' m/s</span>'
+'<span data-l="向 ">'+Math.round(p.track)+'°</span></div>';}
box.innerHTML=h;}
function lgPager(){
const box=q('lgpage');if(!box)return;
const mine=lgMatched||0;
let pages=lgPageCount();
if(lgPage+1>pages)pages=lgPage+1;
let from=0;
if(pages>5)from=Math.min(Math.max(lgPage-2,0),pages-5);
const to=Math.min(pages,from+5);
let h='<button type="button" data-pg="'+(lgPage-1)+'"'+(lgPage<=0?' disabled':'')
+'>上一页</button>';
for(let i=from;i<to;i++){
h+='<button type="button" data-pg="'+i+'"'+(i===lgPage?' class="on"':'')+'>'+(i+1)+'</button>';}
h+='<button type="button" data-pg="'+(lgPage+1)+'"'+(lgPage>=pages-1?' disabled':'')
+'>下一页</button>';
h+='<span class="info">第 '+(lgPage+1)+' / '+pages+' 页 · 每页 '+LG_PER+' 条 · 共 '+mine+' 条'
+(mine>LG_PER*LG_MAXP?('（只保留最近 '+(LG_PER*LG_MAXP)+' 条，更早的请导出 CSV）'):'')
+'</span>';
box.innerHTML=h;
Array.prototype.forEach.call(box.querySelectorAll('button[data-pg]'),function(b){
b.onclick=function(){
const v=Number(b.getAttribute('data-pg'));
if(!(v>=0)||v===lgPage)return;
lgStop();lgPlay=-1;lgPage=v;
if(lgPages[v]){lgPts=lgPages[v];lgRender();lgPager();lgNow();lgRowMark();}
else lgFetch();};});}
function lgStop(){if(lgTimer){clearInterval(lgTimer);lgTimer=null;}
const b=q('lgpp');if(b)b.textContent='播放';}
function lgPlayToggle(){
if(lgLoading){msg('正在读取全部航线点，稍等一下',false);return;}
if(lgPath.length<2){msg('这个目标只有一个点，无法回放',false);return;}
if(lgTimer){lgStop();return;}
if(lgPlay<0||lgPlay>=lgPath.length-1)lgPlay=0;
const b=q('lgpp');if(b)b.textContent='暂停';
lgSeek(lgPlay);
lgTimer=setInterval(function(){
if(lgPlay>=lgPath.length-1){lgStop();lgNow();return;}
lgSeek(lgPlay+1);},Math.round((lgPath.length>150?280:420)/lgRate));}
function lgSeek(i){
if(i<0||i>=lgPath.length)return;
lgPlay=i;
const p=lgPath[i];
if(p&&p.pg!==lgPage&&lgPages[p.pg]){lgPage=p.pg;lgPts=lgPages[p.pg];lgRender();lgPager();}
lgNow();lgDraw(true);lgRowMark();}
function lgReset(){lgStop();lgPlay=-1;lgNow();lgDraw(true);lgRowMark();}
function lgSetRate(v){lgRate=Number(v)||1;
if(lgTimer){lgStop();lgPlayToggle();}}
function lgNow(){
const el=q('lgnow');if(!el)return;
if(!lgPath.length){el.textContent='';return;}
if(lgPlay<0){
if(lgLoading){el.textContent='正在合并全部航线点（已 '+lgPath.length+' 条）…';return;}
el.textContent='航线 '+lgPath.length+' 条'
+(lgPath.length>LG_PER?('（'+lgPageCount()+' 页合并）'):'')+'，点「播放」按时间顺序回放';
return;}
const p=lgPath[lgPlay];
el.textContent=fmtT(p.t,p.tick)+'  '+(lgPlay+1)+'/'+lgPath.length
+'  高 '+Math.round(p.alt)+' m  速 '+Number(p.spd_h).toFixed(1)+' m/s  向 '
+Math.round(p.track)+'°';}
function lgRowMark(){
const box=q('lgpoints');if(!box)return;
const cur=(lgPlay>=0&&lgPath[lgPlay]&&lgPath[lgPlay].pg===lgPage)?lgPath[lgPlay].ri:-1;
Array.prototype.forEach.call(box.querySelectorAll('.lgpt[data-i]'),function(r){
r.className='lgpt'+(Number(r.getAttribute('data-i'))===cur?' cur':'');});}
function lgMapTry(){
if(!lgPath.length)return false;
if(!(window.RXMAP&&typeof RXMAP.replay==='function'))return false;
const mb=q('lgmap');
if(!mb||mb.offsetParent===null||mb.clientWidth<40)return false;
try{return !!RXMAP.replay('lgmap',lgPath);}catch(e){return false;}}
function lgPathDraw(){
if(!lgPath.length)return;
if(lgTimer){lgDraw(true);return;}
if(lgHasMap){
if(window.RXMAP&&RXMAP.replay){
try{if(!RXMAP.replay('lgmap',lgPath))lgHasMap=false;}catch(e){lgHasMap=false;}
}
}else lgHasMap=lgMapTry();
lgDraw(false);}
function lgDraw(move){
try{
const cv=q('lgcv'),mb=q('lgmap');
if(!mb||mb.offsetParent===null)return;
if(!lgHasMap)lgHasMap=lgMapTry();
if(lgHasMap){
if(cv)cv.style.display='none';
mb.style.display='';
if(move&&window.RXMAP&&RXMAP.replayMark){
try{RXMAP.replayMark(lgPlay);}catch(e){}}
return;
}
if(cv)cv.style.display='';
mb.style.display='none';
lgCanvas();
}catch(e){}}
function lgResize(){
try{if(window.RXMAP&&RXMAP.resize)RXMAP.resize();}catch(e){}
if(!lgHasMap&&lgPath.length)lgHasMap=lgMapTry();
lgDraw(false);}
function lgCanvas(){
const cv=q('lgcv');if(!cv||!lgPath.length)return;
const w=Math.max(1,Math.round(cv.clientWidth||320));
const h=Math.max(1,Math.round(cv.clientHeight||230));
const dpr=Math.min(window.devicePixelRatio||1,2);
if(cv.width!==Math.round(w*dpr)||cv.height!==Math.round(h*dpr)){
cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);}
const g=cv.getContext('2d');if(!g)return;
g.setTransform(dpr,0,0,dpr,0,0);
g.fillStyle='#f4f7fb';g.fillRect(0,0,w,h);
g.strokeStyle='#e3e9f0';g.lineWidth=1;g.beginPath();
for(let x=0.5;x<w;x+=32){g.moveTo(x,0);g.lineTo(x,h);}
for(let y=0.5;y<h;y+=32){g.moveTo(0,y);g.lineTo(w,y);}
g.stroke();
let mnla=90,mxla=-90,mnlo=180,mxlo=-180,n=0;
for(let i=0;i<lgPath.length;i++){
const la=Number(lgPath[i].lat),lo=Number(lgPath[i].lon);
if(!isFinite(la)||!isFinite(lo)||(la===0&&lo===0))continue;
n++;
if(la<mnla)mnla=la;if(la>mxla)mxla=la;
if(lo<mnlo)mnlo=lo;if(lo>mxlo)mxlo=lo;}
g.font='12px sans-serif';g.fillStyle='#8b95a1';
if(!n){g.fillText('这条航线没有有效坐标',12,24);return;}
const la0=(mnla+mxla)/2,lo0=(mnlo+mxlo)/2;
const kx=Math.cos(la0*Math.PI/180)||1;
const sx=(mxlo-mnlo)*kx*111320,sy=(mxla-mnla)*111320;
const pad=26;
const sc=Math.min((w-pad*2)/Math.max(sx,1e-3),(h-pad*2)/Math.max(sy,1e-3));
const S=(isFinite(sc)&&sc>0)?sc:1;
function XY(la,lo){
return [w/2+(lo-lo0)*kx*111320*S,h/2-(la-la0)*111320*S];}
function each(f){
for(let i=0;i<lgPath.length;i++){
const la=Number(lgPath[i].lat),lo=Number(lgPath[i].lon);
if(!isFinite(la)||!isFinite(lo)||(la===0&&lo===0))continue;
f(XY(la,lo),i);}}
g.strokeStyle='rgba(21,101,192,.9)';g.lineWidth=3;g.lineJoin='round';
g.beginPath();let st=false;
each(function(p){if(!st){g.moveTo(p[0],p[1]);st=true;}else g.lineTo(p[0],p[1]);});
g.stroke();
g.fillStyle='#1565c0';
each(function(p){g.beginPath();g.arc(p[0],p[1],2.5,0,6.284);g.fill();});
each(function(p,i){
if(i!==0&&i!==lgPath.length-1)return;
g.fillStyle=(i===0)?'#0f8a4d':'#c0392b';
g.beginPath();g.arc(p[0],p[1],6,0,6.284);g.fill();
g.strokeStyle='#fff';g.lineWidth=2;g.stroke();});
if(lgPlay>=0&&lgPath[lgPlay]){
const cla=Number(lgPath[lgPlay].lat),clo=Number(lgPath[lgPlay].lon);
if(isFinite(cla)&&isFinite(clo)&&!(cla===0&&clo===0)){
const cp=XY(cla,clo);
g.fillStyle='#fff';g.strokeStyle='#1565c0';g.lineWidth=3;
g.beginPath();g.arc(cp[0],cp[1],7,0,6.284);g.fill();g.stroke();}}
const cand=[10,20,50,100,200,500,1000,2000,5000];
let Lm=10;
for(let i=0;i<cand.length;i++){if(cand[i]*S<=90)Lm=cand[i];}
g.strokeStyle='#475569';g.lineWidth=2;g.beginPath();
g.moveTo(12,h-14);g.lineTo(12+Lm*S,h-14);g.stroke();
g.fillStyle='#475569';g.font='11px sans-serif';
g.fillText(Lm>=1000?(Lm/1000+' km'):(Lm+' m'),12,h-20);
g.fillText('N',w-18,20);
g.beginPath();g.moveTo(w-13,26);g.lineTo(w-13,38);g.stroke();
g.beginPath();g.moveTo(w-13,22);g.lineTo(w-16,30);g.lineTo(w-10,30);
g.closePath();g.fillStyle='#475569';g.fill();}
async function logRefresh(){
if(roleTx())return;
try{const r=await fetch('/api/log/info');const j=await r.json();
const b=q('logbox');
if(!j.ready){if(b)b.style.display='none';return;}
if(b)b.style.display='';
if(q('lgcnt'))q('lgcnt').textContent=j.count;
if(q('lgused'))q('lgused').textContent=fmtKB(j.used)+' / '+fmtKB(j.total);
if(q('lgrange'))q('lgrange').textContent=j.count?
fmtT(j.oldest_unix,j.oldest_tick)+' → '+fmtT(j.newest_unix,j.newest_tick):'暂无记录';
if(q('lgfill'))q('lgfill').style.width=(j.capacity?(j.count*100/j.capacity):0).toFixed(1)+'%';
if(j.en!==undefined)lgSw(j.en);
logTargets(j);
}catch(e){}}
function logExport(){
if(bleOn){msg('导出 CSV 需要连回热点（蓝牙只传查询数据）',false);return;}
const a=document.createElement('a');
a.href='/api/log/export'+(lgMac?('?mac='+encodeURIComponent(lgMac)):'');
a.download=lgMac?('crid_track_'+String(lgMac).replace(/:/g,'')+'.csv'):'crid_tracks.csv';
document.body.appendChild(a);a.click();a.remove();}
async function logClear(){
if(!confirm('确定清空全部历史轨迹记录？清空后无法恢复。'))return;
try{const r=await fetch('/api/log/clear',{method:'POST'});const j=await r.json();
if(j.ok){logClose();
logRefresh();msg('历史记录已清空',true);}
else{msg('清空失败: '+(j.error||'未知错误'),false);}
}catch(e){msg('清空失败: '+e,false);}}
async function logDel(mac){
if(!confirm('删除 '+String(mac).toUpperCase()+' 的全部历史轨迹？\n删除后该目标会从列表消失，其他目标不受影响。'))return;
try{const r=await fetch('/api/log/del',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify({mac:mac})});const j=await r.json();
if(j.ok){if(lgMac===mac)logClose();logRefresh();msg('已删除该目标的历史记录',true);}
else{msg('删除失败: '+(j.error||'未知错误'),false);}
}catch(e){msg('删除失败: '+e,false);}}
async function syncTime(){sT=Date.now();tNext=sT+20000;var bad='请求失败';var j=null;
try{var r;
if(bleOn){r=await fetch('/api/time?ms='+sT);j=await r.json();}
else{r=await fetch('/api/time',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch_ms:sT})});j=await r.json();}
}catch(e){bad=(e&&e.message)||'请求失败';j=null;}
if(j&&j.ok){tTries=0;tNext=0;tLast=1;tStat(true,j.now);
if(!tDone){tDone=true;msg('设备时间已校准：'+tClock(j.now)+'，轨迹按真实时间写入',true);}
return;}
if(j&&!j.ok)bad='设备拒绝了这个时间（检查手机系统时间是否准确）';
tTries++;tBad=bad;tStat(false,0);
if(tTries<3){msg('授时未完成（'+bad+'），正在重试 '+tTries+'/2…',false);
setTimeout(syncTime,2000*tTries);}
else{tTries=0;tNext=Date.now()+60000;msg('授时失败：'+bad,false);}}
var BLE_SVC='6e400001-b5a3-f393-e0a9-e50e24dcca9e';
var BLE_RXC='6e400002-b5a3-f393-e0a9-e50e24dcca9e';
var BLE_TXC='6e400003-b5a3-f393-e0a9-e50e24dcca9e';
var bleDev=null,bleRx=null,bleTx=null,bleOn=false,bleQ=Promise.resolve();
var bleWait=null,bleBytes=[],bleTimer=null,bleNotifyOn=false,bleRetry=0;
function bleSecure(){return location.protocol==='https:'&&!!navigator.bluetooth;}
function bleUI(){var bar=q('blebar'),t=q('bletxt'),b=q('blebtn'),o=q('bleoff'),l=q('blelnk');if(!bar)return;
var http=(location.protocol!=='https:'),nb=!navigator.bluetooth;
bar.classList.toggle('on',bleOn);
if(t)t.textContent=bleOn?'蓝牙已连接，可以关掉 Wi-Fi 用流量看地图':
(http?'蓝牙取数需要 https 打开（首次会提示证书不安全，点「继续前往」）':
(nb?'这个浏览器不支持网页蓝牙（iPhone 自带的 Safari 不支持）':'蓝牙未连接'));
if(l)l.style.display=(!bleOn&&http)?'':'none';
if(b){b.style.display=(bleOn||http||nb)?'none':'';
if(!bleOn&&bleDev)b.textContent='重新连接';}
if(o)o.style.display=bleOn?'':'none';}
async function bleAttach(dev){
if(!dev||!dev.gatt)throw new Error('设备不可用');
bleDev=dev;
try{dev.removeEventListener('gattserverdisconnected',bleDown);}catch(e){}
dev.addEventListener('gattserverdisconnected',bleDown);
const srv=await dev.gatt.connect();
const svc=await srv.getPrimaryService(BLE_SVC);
bleRx=await svc.getCharacteristic(BLE_RXC);
bleTx=await svc.getCharacteristic(BLE_TXC);
if(!bleNotifyOn){await bleTx.startNotifications();
bleTx.addEventListener('characteristicvaluechanged',bleData);bleNotifyOn=true;}
bleOn=true;bleRetry=0;bleUI();msg('蓝牙已连接，现在可以关闭 Wi-Fi',true);}
async function bleGo(){
if(location.protocol!=='https:'){location.href='https://'+location.host+'/';return;}
if(!navigator.bluetooth){msg('这个浏览器不支持网页蓝牙（iPhone 自带的 Safari 不支持）',false);return;}
try{
if(!bleDev&&navigator.bluetooth.getDevices){
try{const ds=await navigator.bluetooth.getDevices();
for(let i=0;i<ds.length;i++){const n=ds[i].name||'';
if(n.indexOf('ESP32-CRID')===0){bleDev=ds[i];break;}}}catch(e){}}
if(!bleDev){bleDev=await navigator.bluetooth.requestDevice({filters:[{namePrefix:'ESP32-CRID'}],
optionalServices:[BLE_SVC]});}
await bleAttach(bleDev);
}catch(e){msg('蓝牙连接失败: '+((e&&e.message)||e),false);}}
function bleDown(){bleOn=false;bleRx=null;bleTx=null;bleNotifyOn=false;bleBytes=[];bleWait=null;bleUI();
if(bleDev&&bleRetry<5){bleRetry++;
setTimeout(function(){if(bleOn||!bleDev)return;
bleAttach(bleDev).catch(function(){});},1200);}}
function bleStop(){bleRetry=9;
try{if(bleDev&&bleDev.gatt.connected)bleDev.gatt.disconnect();}catch(e){}
bleDown();msg('蓝牙已断开',true);}
function bleData(ev){const dv=ev.target.value,t=dv.getUint8(0);let i;
if(t===1){bleBytes=[];}
else if(t===2){for(i=1;i<dv.byteLength;i++)bleBytes.push(dv.getUint8(i));}
else if(t===3){let txt='';try{txt=new TextDecoder('utf-8').decode(new Uint8Array(bleBytes));}
catch(e){}bleBytes=[];if(bleWait){const f=bleWait;bleWait=null;f(txt);}}
else if(t===4){bleBytes=[];if(bleWait){const g=bleWait;bleWait=null;g(null);}}}
function bleDo(path){return new Promise(function(res,rej){
if(!bleOn||!bleRx)return rej(new Error('蓝牙未连接'));
bleWait=function(txt){if(txt===null)return rej(new Error('设备返回错误'));
res({ok:true,json:async function(){return JSON.parse(txt);},text:async function(){return txt;}});};
bleRx.writeValue(new TextEncoder().encode(path)).catch(function(e){bleWait=null;rej(e);});
clearTimeout(bleTimer);
bleTimer=setTimeout(function(){if(bleWait){bleWait=null;rej(new Error('蓝牙超时'));}},8000);});}
function bleFetch(path){const p=bleQ.then(function(){return bleDo(path);});
bleQ=p.catch(function(){});return p;}
var _fetch=window.fetch?window.fetch.bind(window):null;
window.fetch=function(u,o){
const s=typeof u==='string'?u:((u&&u.url)||'');
const m=(o&&o.method?String(o.method):'GET').toUpperCase();
const isApi=s.indexOf('/api/')===0;
if(bleOn&&isApi&&m!=='GET')return Promise.reject(new Error('蓝牙模式下不能改配置，请连回热点'));
if(bleOn&&isApi)return bleFetch(s);
return _fetch(u,o);};
bleUI();
syncTime();
load().then(function(){otaLoad();logRefresh();},function(){otaLoad();logRefresh();});
setInterval(poll,2000);
setInterval(logRefresh,8000);
