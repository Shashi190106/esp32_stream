const http = require("http");
const { WebSocketServer } = require("ws");

const PORT = process.env.PORT || 10000;
const HOST = "0.0.0.0";

let cameraSocket = null;
const viewers = new Set();

let latestFrame = null;
let latestPhoto = null;
let latestPhotoTime = 0;

let flashState = "OFF";
let lastFrameTime = 0;
let lastSensorTime = 0;

const sensors = {
  pir: false,
  flame: false,
  smoke: false,
  smokeValue: 0,
  humidity: null,
  temperature: null,
  event: "System Started",
  device_ip: "-"
};

const DASHBOARD = `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Home Monitoring</title>
<style>
:root{--bg:#08111f;--card:#111d30;--border:#243653;--text:#edf4ff;--muted:#9db0c9;--ok:#39d98a;--bad:#ff5d6c}
*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(135deg,#07101c,#12213a);color:var(--text)}
header{max-width:1200px;margin:auto;padding:24px 18px 10px}h1{margin:0;font-size:30px}.sub{color:var(--muted);margin-top:6px}
main{max-width:1200px;margin:auto;padding:14px 18px 35px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:18px;box-shadow:0 10px 30px #0005}
.label{font-size:14px;color:var(--muted);margin-bottom:9px}.value{font-size:27px;font-weight:800}.ok{color:var(--ok)}.bad{color:var(--bad)}
.camera{margin-top:14px}.box{background:#000;border-radius:12px;min-height:260px;display:flex;align-items:center;justify-content:center;overflow:hidden}.box img{width:100%;display:block}
.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.toolbar button{border:0;border-radius:10px;padding:11px 15px;font-weight:bold;cursor:pointer;color:#fff;background:#243a5e}.toolbar button.primary{background:#246fba}.toolbar button.danger{background:#8c303c}
.meta{display:flex;gap:16px;flex-wrap:wrap;color:var(--muted);font-size:13px;margin-top:10px}.note{color:var(--muted);font-size:12px;margin-top:15px}
#lastPhoto{width:100%;max-width:900px;border-radius:12px;display:block;margin:auto;background:#000}
</style>
</head>
<body>
<header><h1>🏠 SMART HOME MONITORING</h1><div class="sub">ESP32 DevKit + PIR + Flame + MQ-2 + DHT11 + ESP32-CAM</div></header>
<main>
<section class="grid">
<div class="card"><div class="label">🚶 PIR Motion</div><div id="pir" class="value">--</div></div>
<div class="card"><div class="label">🔥 Flame Sensor</div><div id="flame" class="value">--</div></div>
<div class="card"><div class="label">💨 MQ-2 Smoke</div><div id="smoke" class="value">--</div><div class="note">Raw value: <span id="smokeValue">--</span></div></div>
<div class="card"><div class="label">💧 Humidity</div><div id="humidity" class="value">--</div></div>
<div class="card"><div class="label">🌡 Temperature</div><div id="temperature" class="value">--</div></div>
<div class="card"><div class="label">📡 ESP32 Sensor</div><div id="esp" class="value bad">OFFLINE</div></div>
</section>

<section class="card camera">
<div class="label">📷 LIVE CAMERA</div>
<div class="box"><img id="cam" alt="Waiting for ESP32-CAM"></div>
<div class="meta"><span id="camStatus">Camera: WAITING</span><span id="fps">Viewer FPS: 0.0</span><span id="flash">Flash: OFF</span></div>
<div class="toolbar">
<button class="primary" onclick="capture()">📸 Capture Photo</button>
<button onclick="flashCmd('on')">💡 Flash ON</button>
<button class="danger" onclick="flashCmd('off')">💡 Flash OFF</button>
</div>
</section>

<section class="card camera">
<div class="label">📸 LAST CAPTURED PHOTO</div>
<img id="lastPhoto" alt="No captured photo yet">
<div id="photoInfo" class="note">No captured photo yet</div>
</section>

<section class="card camera">
<div class="label">⚠️ LAST EVENT</div>
<div id="event" class="value">System Started</div>
</section>

<div class="note">Global dashboard hosted on Render.</div>
</main>

<script>
const cam=document.getElementById('cam');
let ws=null,lastUrl=null,frames=0,tick=performance.now(),photoStamp=0;

function connect(){
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/viewer');
  ws.binaryType='blob';
  ws.onopen=()=>document.getElementById('camStatus').textContent='Viewer connected';
  ws.onmessage=e=>{
    if(typeof e.data==='string')return;
    const u=URL.createObjectURL(e.data);
    cam.onload=()=>{if(lastUrl)URL.revokeObjectURL(lastUrl);lastUrl=u};
    cam.src=u;frames++;
  };
  ws.onclose=()=>{document.getElementById('camStatus').textContent='Viewer disconnected - reconnecting...';setTimeout(connect,1500)};
  ws.onerror=()=>{try{ws.close()}catch(e){}};
}

function applyState(id,on,a,b){
  const el=document.getElementById(id);el.textContent=on?a:b;el.className='value '+(on?'bad':'ok');
}

async function refresh(){
  try{
    const s=await fetch('/api/state',{cache:'no-store'}).then(r=>r.json());
    const online=s.sensor_online;
    const esp=document.getElementById('esp');
    esp.textContent=online?'ONLINE':'OFFLINE';esp.className='value '+(online?'ok':'bad');
    applyState('pir',!!s.sensors.pir,'MOTION','NO MOTION');
    applyState('flame',!!s.sensors.flame,'FLAME','NORMAL');
    applyState('smoke',!!s.sensors.smoke,'SMOKE','NORMAL');
    document.getElementById('smokeValue').textContent=s.sensors.smokeValue ?? '--';
    document.getElementById('humidity').textContent=s.sensors.humidity==null?'--':Number(s.sensors.humidity).toFixed(1)+' %';
    document.getElementById('temperature').textContent=s.sensors.temperature==null?'--':Number(s.sensors.temperature).toFixed(1)+' °C';
    document.getElementById('event').textContent=s.sensors.event||'System Started';
    document.getElementById('flash').textContent='Flash: '+s.flash;
    document.getElementById('camStatus').textContent=s.camera_online?'Camera: ONLINE':'Camera: WAITING';
    if(s.photo_time && s.photo_time!==photoStamp){
      photoStamp=s.photo_time;
      document.getElementById('lastPhoto').src='/last-photo?t='+Date.now();
      document.getElementById('photoInfo').textContent='Last photo: '+new Date(s.photo_time*1000).toLocaleString();
    }
  }catch(e){}
}

async function flashCmd(v){
  try{await fetch('/flash/'+v,{method:'POST'})}catch(e){}
}
async function capture(){
  try{
    const r=await fetch('/trigger',{method:'POST'});
    document.getElementById('camStatus').textContent=await r.text();
  }catch(e){document.getElementById('camStatus').textContent='Capture command failed'}
}
setInterval(()=>{const n=performance.now();document.getElementById('fps').textContent='Viewer FPS: '+(frames*1000/Math.max(1,n-tick)).toFixed(1);frames=0;tick=n},1000);
setInterval(refresh,1000);refresh();connect();
</script>
</body>
</html>`;

function send(res, status, body, contentType = "text/plain") {
  res.writeHead(status, {
    "Content-Type": contentType,
    "Cache-Control": "no-store",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type"
  });
  res.end(body);
}

function readJson(req) {
  return new Promise((resolve) => {
    let body = "";
    req.on("data", chunk => {
      body += chunk;
      if (body.length > 100000) req.destroy();
    });
    req.on("end", () => {
      try { resolve(JSON.parse(body || "{}")); }
      catch { resolve({}); }
    });
  });
}

function cameraOnline() {
  return lastFrameTime > 0 && Date.now() - lastFrameTime < 6000;
}

function sensorOnline() {
  return lastSensorTime > 0 && Date.now() - lastSensorTime < 6000;
}

function sendCameraCommand(command) {
  if (!cameraSocket || cameraSocket.readyState !== 1) return false;
  try {
    cameraSocket.send(command);
    console.log("Camera command:", command);
    return true;
  } catch (e) {
    console.log("Command error:", e.message);
    return false;
  }
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://" + (req.headers.host || "localhost"));

  if (req.method === "OPTIONS") {
    return send(res, 204, "");
  }

  if (req.method === "GET" && url.pathname === "/") {
    return send(res, 200, DASHBOARD, "text/html; charset=utf-8");
  }

  if (req.method === "GET" && url.pathname === "/health") {
    return send(res, 200, JSON.stringify({
      ok: true,
      camera_connected: cameraOnline(),
      sensor_connected: sensorOnline(),
      viewers: viewers.size,
      flash: flashState
    }), "application/json");
  }

  if (req.method === "GET" && url.pathname === "/api/state") {
    return send(res, 200, JSON.stringify({
      sensor_online: sensorOnline(),
      camera_online: cameraOnline(),
      flash: flashState,
      photo_time: latestPhotoTime,
      sensors
    }), "application/json");
  }

  if (req.method === "POST" && url.pathname === "/sensor") {
    const data = await readJson(req);
    for (const k of ["pir","flame","smoke","smokeValue","humidity","temperature","event","device_ip"]) {
      if (Object.prototype.hasOwnProperty.call(data, k)) sensors[k] = data[k];
    }
    lastSensorTime = Date.now();
    return send(res, 200, JSON.stringify({ok:true}), "application/json");
  }

  if (req.method === "POST" && url.pathname === "/trigger") {
    const ok = sendCameraCommand("CAPTURE_NOW");
    return send(res, ok ? 200 : 503, ok ? "Capture command sent to camera" : "Camera not connected");
  }

  if (req.method === "POST" && url.pathname === "/flash/on") {
    flashState = "ON";
    const ok = sendCameraCommand("FLASH_ON");
    return send(res, 200, ok ? "Flash ON command sent" : "Flash ON saved; camera not connected");
  }

  if (req.method === "POST" && url.pathname === "/flash/off") {
    flashState = "OFF";
    const ok = sendCameraCommand("FLASH_OFF");
    return send(res, 200, ok ? "Flash OFF command sent" : "Flash OFF saved; camera not connected");
  }

  if (req.method === "GET" && url.pathname === "/last-photo") {
    if (!latestPhoto) return send(res, 404, "No captured photo yet");
    res.writeHead(200, {
      "Content-Type": "image/jpeg",
      "Content-Length": latestPhoto.length,
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": "*"
    });
    return res.end(latestPhoto);
  }

  return send(res, 404, "Not found");
});

const wss = new WebSocketServer({ noServer: true });

server.on("upgrade", (req, socket, head) => {
  const url = new URL(req.url, "http://" + (req.headers.host || "localhost"));

  if (url.pathname !== "/ws" && url.pathname !== "/viewer") {
    socket.destroy();
    return;
  }

  wss.handleUpgrade(req, socket, head, ws => {
    wss.emit("connection", ws, req, url.pathname);
  });
});

wss.on("connection", (ws, req, path) => {
  ws.binaryType = "arraybuffer";

  if (path === "/viewer") {
    viewers.add(ws);

    if (latestFrame) {
      try { ws.send(latestFrame); } catch {}
    }

    console.log("Viewer connected. Viewers:", viewers.size);

    ws.on("close", () => {
      viewers.delete(ws);
      console.log("Viewer disconnected. Viewers:", viewers.size);
    });

    ws.on("error", () => {
      viewers.delete(ws);
    });

    return;
  }

  // /ws is the camera connection
  if (cameraSocket && cameraSocket.readyState === 1) {
    try { cameraSocket.close(); } catch {}
  }

  cameraSocket = ws;

  console.log("CAMERA WSS CONNECTED from", req.socket.remoteAddress);

  try { ws.send("FLASH_" + flashState); } catch {}

  let expectingPhoto = false;

  ws.on("message", (data, isBinary) => {
    if (!isBinary) {
      const message = data.toString();

      if (message === "PHOTO") {
        expectingPhoto = true;
        console.log("Camera marked next frame as PHOTO");
      }

      return;
    }

    const buffer = Buffer.from(data);

    if (expectingPhoto) {
      latestPhoto = buffer;
      latestPhotoTime = Date.now();
      expectingPhoto = false;
      console.log("LAST PHOTO RECEIVED:", buffer.length, "bytes");
      return;
    }

    latestFrame = buffer;
    lastFrameTime = Date.now();

    for (const viewer of viewers) {
      if (viewer.readyState === 1) {
        try { viewer.send(buffer); } catch {}
      }
    }
  });

  ws.on("close", () => {
    if (cameraSocket === ws) cameraSocket = null;
    console.log("CAMERA WSS DISCONNECTED");
  });

  ws.on("error", err => {
    console.log("CAMERA WSS ERROR:", err.message);
  });
});

setInterval(() => {
  // Server-side ping for viewer/camera sockets.
  for (const ws of [cameraSocket, ...viewers]) {
    if (ws && ws.readyState === 1) {
      try { ws.ping(); } catch {}
    }
  }
}, 30000);

server.listen(PORT, HOST, () => {
  console.log(`Smart Home server listening on ${HOST}:${PORT}`);
});
