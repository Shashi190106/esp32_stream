import time
from io import BytesIO
from threading import Lock
from flask import Flask, render_template_string, request, send_file, jsonify
from flask_sock import Sock

app = Flask(__name__)
sock = Sock(app)

@app.after_request
def add_cors_headers(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET,POST,OPTIONS"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    response.headers["Cache-Control"] = "no-store"
    return response

state_lock = Lock()

latest_frame = None
latest_photo = None
latest_photo_time = 0.0

camera_ws = None
viewer_sockets = set()

flash_state = "OFF"

last_frame_time = 0.0
last_sensor_time = 0.0

sensor_state = {
    "pir": False,
    "flame": False,
    "smoke": False,
    "smokeValue": 0,
    "humidity": None,
    "temperature": None,
    "event": "System Started",
    "device_ip": "-"
}

PAGE = r'''<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Home Monitoring</title>
<style>
:root{
  --bg:#0b1220; --panel:#121c2e; --panel2:#0f1727; --text:#eef4ff;
  --muted:#9eb0c7; --accent:#4da3ff; --good:#39d98a; --bad:#ff5d6c;
  --warn:#ffbf4d; --border:#24334c;
}
*{box-sizing:border-box}
body{
  margin:0;font-family:Inter,Arial,sans-serif;background:linear-gradient(135deg,#08101d,#111c30);
  color:var(--text);min-height:100vh;
}
header{
  padding:24px 18px 10px;max-width:1200px;margin:auto;
}
h1{margin:0;font-size:30px}
.subtitle{color:var(--muted);margin-top:7px}
.wrap{max-width:1200px;margin:auto;padding:14px 18px 30px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px}
.card{
  background:rgba(18,28,46,.95);border:1px solid var(--border);
  border-radius:16px;padding:18px;box-shadow:0 12px 32px rgba(0,0,0,.18)
}
.card h3{margin:0 0 10px;font-size:15px;color:var(--muted);font-weight:600}
.value{font-size:28px;font-weight:800}
.small{font-size:13px;color:var(--muted);margin-top:7px}
.ok{color:var(--good)} .bad{color:var(--bad)} .warn{color:var(--warn)}
.camera-card{margin-top:14px}
.camera-box{
  width:100%;background:#000;border-radius:12px;overflow:hidden;
  min-height:260px;display:flex;align-items:center;justify-content:center
}
#cam{width:100%;display:block}
.photo{
  width:100%;max-width:900px;border-radius:12px;display:block;margin:auto;background:#000;
}
.controls{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
button{
  border:0;border-radius:10px;padding:11px 15px;font-weight:800;cursor:pointer;
  background:#223351;color:#fff
}
button:hover{filter:brightness(1.12)}
button.primary{background:#2469b5}
button.danger{background:#8f2f3a}
.statusbar{
  margin-top:14px;display:flex;flex-wrap:wrap;gap:14px;color:var(--muted);font-size:13px
}
.footer{color:var(--muted);font-size:12px;margin-top:18px}
@media(max-width:600px){h1{font-size:24px}.value{font-size:24px}}
</style>
</head>
<body>
<header>
  <h1>🏠 SMART HOME MONITORING</h1>
  <div class="subtitle">ESP32 + PIR + Flame + MQ-2 + DHT11 + ESP32-CAM</div>
</header>

<div class="wrap">

  <div class="grid">
    <div class="card">
      <h3>🚶 PIR Motion</h3>
      <div id="pir" class="value">--</div>
      <div class="small" id="pirHint">Waiting for ESP32</div>
    </div>

    <div class="card">
      <h3>🔥 Flame Sensor</h3>
      <div id="flame" class="value">--</div>
      <div class="small">Alarm when active</div>
    </div>

    <div class="card">
      <h3>💨 Smoke Sensor</h3>
      <div id="smoke" class="value">--</div>
      <div class="small">Raw MQ-2: <span id="smokeValue">--</span></div>
    </div>

    <div class="card">
      <h3>💧 Humidity</h3>
      <div id="humidity" class="value">--</div>
      <div class="small">DHT11</div>
    </div>

    <div class="card">
      <h3>🌡 Temperature</h3>
      <div id="temperature" class="value">--</div>
      <div class="small">DHT11</div>
    </div>

    <div class="card">
      <h3>📡 ESP32 Status</h3>
      <div id="espStatus" class="value">OFFLINE</div>
      <div class="small">Cloud sensor link</div>
    </div>
  </div>

  <div class="card camera-card">
    <h3>📷 Live Camera</h3>
    <div class="camera-box">
      <img id="cam" alt="Waiting for ESP32-CAM...">
    </div>
    <div class="statusbar">
      <span id="cameraStatus">Camera: WAITING</span>
      <span id="viewerFps">Viewer FPS: 0.0</span>
      <span id="flashStatus">Flash: OFF</span>
    </div>
    <div class="controls">
      <button class="primary" onclick="capturePhoto()">📸 Capture Photo</button>
      <button onclick="flashCmd('on')">💡 Flash ON</button>
      <button onclick="flashCmd('off')">💡 Flash OFF</button>
    </div>
  </div>

  <div class="card camera-card">
    <h3>📸 Last Captured Photo</h3>
    <img id="lastPhoto" class="photo" alt="No motion photo captured yet">
    <div class="small" id="photoTime">No captured photo yet</div>
  </div>

  <div class="card camera-card">
    <h3>⚠️ Last Event</h3>
    <div id="event" class="value">System Started</div>
  </div>

  <div class="footer">
    Global dashboard hosted on Render. Sensor board and ESP32-CAM connect outward over Wi-Fi.
  </div>

</div>

<script>
const cam = document.getElementById('cam');
const cameraStatus = document.getElementById('cameraStatus');
const viewerFps = document.getElementById('viewerFps');
const flashStatus = document.getElementById('flashStatus');

let ws = null;
let frames = 0;
let tick = performance.now();
let oldUrl = null;
let lastPhotoTime = 0;

function connectViewer(){
  ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/viewer');
  ws.binaryType = 'blob';

  ws.onopen = () => {
    cameraStatus.textContent = 'Camera viewer connected';
  };

  ws.onmessage = (e) => {
    if (typeof e.data === 'string') return;
    const url = URL.createObjectURL(e.data);
    cam.onload = () => {
      if(oldUrl) URL.revokeObjectURL(oldUrl);
      oldUrl = url;
    };
    cam.src = url;
    frames++;
  };

  ws.onclose = () => {
    cameraStatus.textContent = 'Viewer disconnected - reconnecting...';
    setTimeout(connectViewer, 1000);
  };

  ws.onerror = () => {
    try { ws.close(); } catch(e) {}
  };
}

async function flashCmd(v){
  try{
    const r = await fetch('/flash/' + v, {method:'POST'});
    flashStatus.textContent = 'Flash: ' + (v === 'on' ? 'ON' : 'OFF');
    cameraStatus.textContent = await r.text();
  }catch(e){
    cameraStatus.textContent = 'Flash command failed';
  }
}

async function capturePhoto(){
  try{
    const r = await fetch('/trigger', {method:'POST'});
    cameraStatus.textContent = await r.text();
  }catch(e){
    cameraStatus.textContent = 'Capture command failed';
  }
}

function applyClass(el, active, activeText, normalText){
  el.textContent = active ? activeText : normalText;
  el.className = 'value ' + (active ? 'bad' : 'ok');
}

async function refreshState(){
  try{
    const r = await fetch('/api/state', {cache:'no-store'});
    const s = await r.json();

    const sensorOnline = s.sensor_online;
    const cameraOnline = s.camera_online;

    document.getElementById('espStatus').textContent = sensorOnline ? 'ONLINE' : 'OFFLINE';
    document.getElementById('espStatus').className = 'value ' + (sensorOnline ? 'ok' : 'bad');

    applyClass(document.getElementById('pir'), s.sensors.pir, 'MOTION', 'NO MOTION');
    document.getElementById('pirHint').textContent = s.sensors.pir ? 'Motion detected' : 'Area clear';

    applyClass(document.getElementById('flame'), s.sensors.flame, 'FLAME', 'NORMAL');
    applyClass(document.getElementById('smoke'), s.sensors.smoke, 'SMOKE', 'NORMAL');

    document.getElementById('smokeValue').textContent = s.sensors.smokeValue;

    document.getElementById('humidity').textContent =
      s.sensors.humidity == null ? '--' : Number(s.sensors.humidity).toFixed(1) + ' %';

    document.getElementById('temperature').textContent =
      s.sensors.temperature == null ? '--' : Number(s.sensors.temperature).toFixed(1) + ' °C';

    document.getElementById('event').textContent = s.sensors.event || 'System Started';

    cameraStatus.textContent = cameraOnline ? 'Camera: ONLINE' : 'Camera: WAITING';
    flashStatus.textContent = 'Flash: ' + s.flash;

    if (s.photo_time && s.photo_time !== lastPhotoTime) {
      lastPhotoTime = s.photo_time;
      document.getElementById('lastPhoto').src = '/last-photo?t=' + Date.now();
      document.getElementById('photoTime').textContent =
        'Last photo received at cloud: ' + new Date(s.photo_time * 1000).toLocaleString();
    }
  }catch(e){
    document.getElementById('espStatus').textContent = 'OFFLINE';
  }
}

setInterval(() => {
  const now = performance.now();
  viewerFps.textContent =
    'Viewer FPS: ' + (frames * 1000 / Math.max(1, now - tick)).toFixed(1);
  frames = 0;
  tick = now;
}, 1000);

setInterval(refreshState, 1000);
refreshState();
connectViewer();
</script>
</body>
</html>'''

@app.get("/")
def index():
    return render_template_string(PAGE)

@app.get("/api/state")
def api_state():
    with state_lock:
        now = time.time()
        sensor_online = (now - last_sensor_time) < 6 if last_sensor_time else False
        camera_online = (now - last_frame_time) < 6 if last_frame_time else False
        return jsonify({
            "sensor_online": sensor_online,
            "camera_online": camera_online,
            "flash": flash_state,
            "photo_time": latest_photo_time,
            "sensors": dict(sensor_state)
        })

@app.get("/status")
def status():
    with state_lock:
        now = time.time()
        camera_online = (now - last_frame_time) < 6 if last_frame_time else False
        sensor_online = (now - last_sensor_time) < 6 if last_sensor_time else False
        return jsonify({
            "camera_online": camera_online,
            "sensor_online": sensor_online,
            "flash": flash_state,
            "viewers": len(viewer_sockets),
            "photo_time": latest_photo_time
        })

def send_camera_command(command):
    with state_lock:
        ws = camera_ws
    if ws is None:
        return False
    try:
        ws.send(command)
        return True
    except Exception:
        return False

@app.post("/flash/on")
def flash_on():
    global flash_state
    with state_lock:
        flash_state = "ON"
    ok = send_camera_command("FLASH_ON")
    return ("Flash ON command sent" if ok else "Flash ON saved; camera not connected"), 200

@app.post("/flash/off")
def flash_off():
    global flash_state
    with state_lock:
        flash_state = "OFF"
    ok = send_camera_command("FLASH_OFF")
    return ("Flash OFF command sent" if ok else "Flash OFF saved; camera not connected"), 200

@app.post("/trigger")
def trigger():
    ok = send_camera_command("CAPTURE_NOW")
    return ("Capture command sent to camera" if ok else "Camera not connected"), (200 if ok else 503)

@app.post("/sensor")
def sensor_update():
    global last_sensor_time
    data = request.get_json(silent=True) or {}

    with state_lock:
        for key in ("pir", "flame", "smoke", "smokeValue", "humidity", "temperature", "event", "device_ip"):
            if key in data:
                sensor_state[key] = data[key]
        last_sensor_time = time.time()

    return jsonify({"ok": True})

@app.get("/last-photo")
def last_photo():
    with state_lock:
        photo = latest_photo
    if not photo:
        return ("No captured photo yet", 404)
    return send_file(
        BytesIO(photo),
        mimetype="image/jpeg",
        max_age=0,
        download_name="last-captured.jpg"
    )

@sock.route("/ws")
def camera(ws):
    global camera_ws, latest_frame, latest_photo, latest_photo_time, last_frame_time

    waiting_for_photo = False

    with state_lock:
        camera_ws = ws
        initial_command = "FLASH_" + flash_state

    try:
        ws.send(initial_command)

        while True:
            data = ws.receive()
            if data is None:
                break

            if isinstance(data, str):
                if data == "PHOTO":
                    waiting_for_photo = True
                continue

            if isinstance(data, bytes):
                now = time.time()

                if waiting_for_photo:
                    with state_lock:
                        latest_photo = bytes(data)
                        latest_photo_time = now
                    waiting_for_photo = False
                else:
                    with state_lock:
                        latest_frame = bytes(data)
                        last_frame_time = now
                        viewers = list(viewer_sockets)

                    dead = []
                    for viewer in viewers:
                        try:
                            viewer.send(data)
                        except Exception:
                            dead.append(viewer)

                    if dead:
                        with state_lock:
                            for viewer in dead:
                                viewer_sockets.discard(viewer)

    except Exception:
        pass
    finally:
        with state_lock:
            if camera_ws is ws:
                camera_ws = None

@sock.route("/viewer")
def viewer(ws):
    with state_lock:
        viewer_sockets.add(ws)

    try:
        last_sent = None

        with state_lock:
            frame = latest_frame
        if frame is not None:
            ws.send(frame)
            last_sent = frame

        while True:
            with state_lock:
                frame = latest_frame

            if frame is not None and frame is not last_sent:
                ws.send(frame)
                last_sent = frame
            else:
                time.sleep(0.01)

    except Exception:
        pass
    finally:
        with state_lock:
            viewer_sockets.discard(ws)

@app.get("/health")
def health():
    with state_lock:
        now = time.time()
        camera_online = (now - last_frame_time) < 6 if last_frame_time else False
        sensor_online = (now - last_sensor_time) < 6 if last_sensor_time else False
        return {
            "ok": True,
            "camera_connected": camera_online,
            "sensor_connected": sensor_online,
            "viewers": len(viewer_sockets),
            "flash": flash_state,
            "photo_time": latest_photo_time
        }

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=10000)
