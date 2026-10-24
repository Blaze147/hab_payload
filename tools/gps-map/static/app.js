const $ = id => document.getElementById(id);
const activityTimeline = new ActivityTimeline($('linkActivity'));
const map = L.map('map', {zoomControl:false}).setView([20,0],2);
L.control.zoom({position:'topright'}).addTo(map);
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom:19, attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
}).addTo(map);
tiles.on('tileerror',()=>{ $('mapError').textContent='Map tiles unavailable. Check internet access; GPS readings continue below.'; $('mapError').style.display='block'; });
tiles.on('tileload',()=>{ $('mapError').style.display='none'; });
let marker, position, first=true;
let browserStorage;
try { browserStorage=window.localStorage; } catch (_) { /* Storage status shown below. */ }
const positionHistory = new PositionHistory(browserStorage);
const historyLayer = L.layerGroup().addTo(map);
function renderHistory(){
  positionHistory.prune();
  historyLayer.clearLayers();
  for(const point of positionHistory.points){
    L.circleMarker([point.lat,point.lon],{radius:5,fillColor:PositionHistory.color(point.time),color:'#fff',weight:1,fillOpacity:1})
      .bindTooltip(`${new Date(point.time).toLocaleTimeString()} · ${Math.floor((Date.now()-point.time)/60000)} min ago`)
      .addTo(historyLayer);
  }
  if(marker) marker.bringToFront();
  $('historyStatus').hidden=!positionHistory.error;
  $('historyStatus').textContent=positionHistory.error
    ? 'Position history is visible, but browser storage is unavailable. Refresh may lose it.'
    : ''; 
}
if(positionHistory.latest){
  const last=positionHistory.latest;
  position=[last.lat,last.lon];
  marker=L.circleMarker(position,{radius:9,fillColor:'#808890',color:'#fff',weight:3,fillOpacity:1}).addTo(map);
  map.setView(position,18);
  $('coords').textContent=`${last.lat.toFixed(6)}, ${last.lon.toFixed(6)}`;
}
renderHistory();
setInterval(renderHistory,30000);

map.on('dragstart',()=>{ $('follow').checked=false; });
$('center').onclick=()=>{ if(position){map.setView(position,18);$('follow').checked=true;} };
const fmt=(n,d=1)=>Number.isFinite(n)?n.toFixed(d):'—';
let imageVersion=null;
function updateCamera(data){
  if(data.source==='ground'){$('cameraCard').hidden=true;return;}
  const camera=data.camera;
  if(camera && camera.status!=='WAITING') $('cameraCard').hidden=false;
  if(camera?.received_at && camera.received_at!==imageVersion){
    imageVersion=camera.received_at;
    $('cameraImage').src=`/camera.jpg?v=${imageVersion}`;
    $('cameraImage').hidden=false;
  }
  if(!data.connected){$('cameraStatus').textContent='Device disconnected · showing last snapshot';return;}
  if(!camera) return;
  const error=['SPI_ERROR','I2C_ERROR','UNSUPPORTED_SENSOR','INIT_ERROR','CAPTURE_TIMEOUT','SIZE_ERROR','JPEG_ERROR','INVALID_FRAME','TRANSFER_TIMEOUT'].includes(camera.status);
  if(error){$('cameraStatus').textContent=`Camera: ${camera.status.replaceAll('_',' ').toLowerCase()}${imageVersion?' · showing last snapshot':''}`;return;}
  if(camera.age_seconds!==null){
    const age=Math.floor(camera.age_seconds);
    $('cameraStatus').textContent=`${camera.width} × ${camera.height} · ${age}s ago · one image/min${age>90?' · overdue':''}`;
  }else $('cameraStatus').textContent=camera.status==='RECEIVING'?'Receiving first JPEG…':'Camera connected · waiting for first snapshot';
}
function updateEnvironment(data){
  const e=data.environment;
  if(e) $('environmentCard').hidden=false;
  const ground=data.source==='ground';
  const fresh=data.connected && data.environment_age_seconds!==null && data.environment_age_seconds<(data.freshness_seconds?.environment??6);
  const valid=e?.status==='OK' && Number.isFinite(e.c) && Number.isFinite(e.humidity_pct);
  $('temperature').textContent=valid?fmt(e.c * 9 / 5 + 32):'—';
  $('humidity').textContent=valid?fmt(e.humidity_pct):'—';
  const age=Number.isFinite(data.environment_age_seconds)?`${Math.floor(data.environment_age_seconds)}s since update`:'';
  const latest=data.environment_latest_status??e?.status;
  const warning=latest==='BAD_PACKET'?'Latest telemetry packet invalid':latest && latest!=='OK'?(ground?'Latest sensor data unavailable':'Latest sensor read failed · retrying'):'';
  const state=!data.connected?'Ground/device disconnected':!e?'Waiting for sensor…':valid && !fresh?'Sensor update stale':'';
  $('environmentStatus').textContent=[age,warning,state].filter(Boolean).join(' · ');
  $('environmentTooltip').textContent=ground?'Received over LoRa · pauses during control windows':'Live sensor · updates every 2 seconds';
}
async function update(){
  try{
    const response=await fetch('/api/gps',{cache:'no-store',signal:AbortSignal.timeout(3000)});
    if(!response.ok) throw Error('Server unavailable');
    const data=await response.json(), s=data.sample;
    const ground=data.source==='ground';
    activityTimeline.accept(data);
    $('sourcePath').textContent=ground?'BALLOON → LORA → GROUND STATION':'BALLOON → USB';
    $('linkSummary').hidden=!ground;
    $('positionSource').textContent=ground?'BALLOON / RECEIVED TELEMETRY':'LIVE POSITION / MAX-M10S';
    $('positionTitle').textContent=ground?'Balloon payload position':'Live GPS location';
    $('environmentSource').textContent=ground?'BALLOON ENVIRONMENT / LORA':'ENVIRONMENT / DHT22';
    $('speedLabel').textContent=ground?'SIGNAL RSSI':'SPEED';
    $('speedUnit').textContent=ground?'dBm':'km/h';
    $('ppsLabel').textContent=ground?'MESSAGES RX':'PPS COUNT';
    updateEnvironment(data);
    updateCamera(data);
    const fresh=data.connected && data.age_seconds!==null && data.age_seconds<(data.freshness_seconds?.gps??4);
    const fixed=fresh && s?.status==='FIX' && Number.isFinite(s.lat) && Number.isFinite(s.lon);
    $('status').textContent=!data.connected?(ground?'Ground station disconnected':'Device disconnected'):!fresh?(ground?'Waiting for balloon telemetry':'Waiting for GPS stream'):fixed?(ground?`Receiving balloon · ${data.radio?.dataProfile??'LoRa'}`:'Live GPS fix'):s?.status==='NO_DATA'?'No GPS data':'Acquiring satellites';
    $('live').classList.toggle('ok',fixed);
    if(fixed){
      position=[s.lat,s.lon];
      positionHistory.accept({lat:s.lat,lon:s.lon,time:data.received_at*1000});
      renderHistory();
      if(!marker) marker=L.circleMarker(position,{radius:9,fillColor:'#168d6d',color:'#fff',weight:3,fillOpacity:1}).addTo(map);
      marker.setLatLng(position).setStyle({fillColor:'#168d6d'});
      if(first){map.setView(position,18);first=false;}
      else if($('follow').checked) map.panTo(position,{animate:false});
      $('coords').textContent=`${s.lat.toFixed(6)}, ${s.lon.toFixed(6)}`;
    }else if(marker){marker.setStyle({fillColor:'#808890'});}
    for(const [id,key,d] of [['alt','alt_m',1],['speed','speed_kmh',1],['sats','sats',0],['pps','pps',0]]){
      $(id).textContent=fresh && s && Number.isFinite(s[key])?fmt(s[key],d):'—';
    }
    if(ground){
      const profile=data.radio?.dataProfile;
      $('profile').textContent=data.connected && /^P[0-4]$/.test(profile)?profile:'—';
      const rawGoodput=data.radio?.airtimeGoodputBps;
      const goodput=rawGoodput===undefined || rawGoodput===null?NaN:Number(rawGoodput);
      $('goodput').textContent=fresh && Number.isFinite(goodput) && goodput>=0?goodput.toLocaleString(undefined,{maximumFractionDigits:1}):'—';
      const signal=Number(data.radio?.rssi);
      $('speed').textContent=fresh && data.radio?.rssi!==undefined && Number.isFinite(signal)?fmt(signal,0):'—';
      $('pps').textContent=fmt(data.received_messages,0);
    }
    const identity=ground?(data.telemetry?`Session ${data.telemetry.session} · message ${data.telemetry.message}`:'No RF messages yet'):(s?.utc && s.utc!=='NA'?s.utc:'UTC pending');
    const link=ground?` · ${data.radio?.established==='1'?'Link established':'Link acquiring'}`:'';
    $('connectionTooltip').textContent=`${data.port || 'USB'} · ${identity}`;
    $('detailText').textContent=data.error || `${data.age_seconds===null?'No samples yet':`${Math.floor(data.age_seconds)}s since update`}${link}${!fixed && position?' · Marker shows last valid fix':''}`;
  }catch(error){
    activityTimeline.disconnect();
    updateEnvironment({connected:false});
    updateCamera({connected:false});
    $('status').textContent='Local server disconnected';$('live').classList.remove('ok');
    for(const id of ['alt','speed','sats','pps','profile','goodput']) $(id).textContent='—';
    if(marker) marker.setStyle({fillColor:'#808890'});
    $('detailText').textContent='Reconnecting to local telemetry bridge…';
  }finally{setTimeout(update,500);}
}
update();
