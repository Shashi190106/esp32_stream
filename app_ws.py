import time
from threading import Lock
from flask import Flask, render_template_string
from flask_sock import Sock

app = Flask(__name__)
sock = Sock(app)

state_lock = Lock()
latest_frame = None
camera_ws = None
viewer_sockets = set()
flash_state = "OFF"
last_frame_time = 0.0

PAGE = '''<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Global Stream</title>
<style>
body{font-family:Arial;background:#111;color:#eee;text-align:center;margin:0;padding:12px}
img{width:min(96vw,960px);height:auto;background:#000;border-radius:8px}
.btn{padding:12px 18px;margin:6px;border:0;border-radius:7px;font-weight:bold;font-size:15px}
.stat{margin:8px;color:#bbb}
</style>
</head>
<body>
<h2>ESP32-CAM Global Stream</h2>
<img id="cam" alt="Waiting for camera...">
<div class="stat" id="fps">Connecting...</div>
<div class="stat" id="status">Camera status: unknown</div>
<button class="btn" onclick="cmd('on')">Flash ON</button>
<button class="btn" onclick="cmd('off')">Flash OFF</button>
<script>
const img=document.getElementById('cam');
const fps=document.getElementById('fps');
const status=document.getElementById('status');

let ws;
let frames=0;
let lastTick=performance.now();
let oldUrl=null;

function connect(){
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/viewer');
  ws.binaryType='blob';

  ws.onopen=()=>{
    status.textContent='Camera status: connected to viewer';
  };

  ws.onmessage=e=>{
    if(typeof e.data==='string') return;

    const url=URL.createObjectURL(e.data);

    img.onload=()=>{
      if(oldUrl) URL.revokeObjectURL(oldUrl);
      oldUrl=url;
    };

    img.src=url;
    frames++;
  };

  ws.onclose=()=>{
    status.textContent='Viewer disconnected; reconnecting...';
    setTimeout(connect,1000);
  };

  ws.onerror=()=>{
    try{ws.close();}catch(e){}
  };
}

async function cmd(value){
  try{
    const r=await fetch('/flash/'+value,{method:'POST'});
    status.textContent=await r.text();
  }catch(e){
    status.textContent='Flash command failed';
  }
}

setInterval(()=>{
  const now=performance.now();
  fps.textContent='Viewer FPS: '+(frames*1000/(now-lastTick)).toFixed(1);
  frames=0;
  lastTick=now;
},1000);

setInterval(()=>{
  fetch('/status')
    .then(r=>r.json())
    .then(s=>{
      status.textContent = s.camera_online
        ? 'Camera: ONLINE | Flash: '+s.flash
        : 'Camera: WAITING';
    })
    .catch(()=>{});
},2000);

connect();
</script>
</body>
</html>'''

@app.get("/")
def index():
    return render_template_string(PAGE)

@app.get("/status")
def status():
    with state_lock:
        online = (time.time() - last_frame_time) < 5 if last_frame_time else False
        return {
            "camera_online": online,
            "flash": flash_state,
            "viewers": len(viewer_sockets)
        }

def send_camera_command(command):
    with state_lock:
        ws = camera_ws

    if ws is not None:
        try:
            ws.send(command)
            return True
        except Exception:
            return False
    return False

@app.post("/flash/on")
def flash_on():
    global flash_state
    with state_lock:
        flash_state = "ON"

    ok = send_camera_command("FLASH_ON")
    return "Flash ON command sent" if ok else "Flash ON saved; camera not connected"

@app.post("/flash/off")
def flash_off():
    global flash_state
    with state_lock:
        flash_state = "OFF"

    ok = send_camera_command("FLASH_OFF")
    return "Flash OFF command sent" if ok else "Flash OFF saved; camera not connected"

@sock.route("/ws")
def camera(ws):
    global camera_ws, latest_frame, last_frame_time

    with state_lock:
        camera_ws = ws
        command = "FLASH_" + flash_state

    try:
        ws.send(command)

        while True:
            data = ws.receive()

            if data is None:
                break

            if isinstance(data, bytes):
                with state_lock:
                    latest_frame = data
                    last_frame_time = time.time()
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

        while True:
            with state_lock:
                frame = latest_frame

            if frame is not None and frame is not last_sent:
                ws.send(frame)
                last_sent = frame
            else:
                time.sleep(0.005)

    except Exception:
        pass

    finally:
        with state_lock:
            viewer_sockets.discard(ws)

@app.get("/health")
def health():
    with state_lock:
        online = (time.time() - last_frame_time) < 5 if last_frame_time else False
        return {
            "ok": True,
            "camera_connected": online,
            "viewers": len(viewer_sockets),
            "flash": flash_state
        }

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=10000)
