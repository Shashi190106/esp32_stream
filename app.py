from flask import Flask, render_template_string, request
from flask_sock import Sock
import threading
import time

app = Flask(__name__)
sock = Sock(app)

app.config['SOCK_SERVER_OPTIONS'] = {
    'ping_interval': 25,
    'max_message_size': 2 * 1024 * 1024,
}

state_lock = threading.Lock()
camera_ws = None
viewer_sockets = set()
flash_command = 'off'
last_frame_time = 0.0

PAGE = '''<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Global Stream</title>
<style>
body{background:#111;color:#fff;font-family:Arial;text-align:center;margin:0;padding:16px}
#view{width:100%;max-width:640px;border:2px solid #fff;border-radius:8px;background:#000;display:block;margin:auto;image-rendering:auto}
button{padding:12px 18px;margin:6px;border:0;border-radius:8px;font-weight:bold;font-size:15px}
#status{margin:10px;font-size:16px}#fps{font-size:14px;color:#bbb}
</style>
</head>
<body>
<h2>ESP32-CAM Global Stream</h2>
<img id="view" alt="Waiting for camera...">
<div>
<button onclick="flash('/flash/on')">🔦 Flash ON</button>
<button onclick="flash('/flash/off')">💡 Flash OFF</button>
</div>
<div id="status">Connecting...</div><div id="fps">FPS: --</div>
<script>
let ws, frames=0, last=performance.now(), oldUrl=null;
const img=document.getElementById('view');
function connect(){
  const p=location.protocol==='https:'?'wss':'ws';
  ws=new WebSocket(p+'://'+location.host+'/ws');
  ws.binaryType='blob';
  ws.onopen=()=>{ws.send('viewer');document.getElementById('status').textContent='Viewer connected';};
  ws.onmessage=e=>{
    if(typeof e.data==='string') return;
    const u=URL.createObjectURL(e.data);
    img.onload=()=>{if(oldUrl)URL.revokeObjectURL(oldUrl);oldUrl=u;};
    img.src=u; frames++;
  };
  ws.onclose=()=>{document.getElementById('status').textContent='Reconnecting...';setTimeout(connect,1000);};
  ws.onerror=()=>ws.close();
}
setInterval(()=>{const now=performance.now();document.getElementById('fps').textContent='FPS: '+(frames*1000/(now-last)).toFixed(1);frames=0;last=now;},1000);
function flash(url){fetch(url).then(r=>r.text()).then(t=>document.getElementById('status').textContent=t).catch(()=>{});}
connect();
</script>
</body>
</html>'''

@app.get('/')
def index():
    return render_template_string(PAGE)

@app.get('/status')
def status():
    with state_lock:
        online = (time.time() - last_frame_time) < 5 if last_frame_time else False
    return {'online': online}

@app.get('/flash/on')
def flash_on():
    global flash_command
    with state_lock:
        flash_command = 'on'
        ws = camera_ws
    if ws:
        try: ws.send('FLASH_ON')
        except Exception: pass
    return 'Flash ON command sent to ESP32-CAM'

@app.get('/flash/off')
def flash_off():
    global flash_command
    with state_lock:
        flash_command = 'off'
        ws = camera_ws
    if ws:
        try: ws.send('FLASH_OFF')
        except Exception: pass
    return 'Flash OFF command sent to ESP32-CAM'

@sock.route('/ws')
def websocket(ws):
    global camera_ws, last_frame_time
    role = ws.receive()
    if role == 'camera':
        with state_lock:
            camera_ws = ws
            command = flash_command
        try:
            ws.send('FLASH_' + command.upper())
            while True:
                data = ws.receive()
                if data is None:
                    break
                if isinstance(data, bytes):
                    with state_lock:
                        last_frame_time = time.time()
                        viewers = list(viewer_sockets)
                        current_command = flash_command
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
                    # Keep the ESP32 informed if the browser changed flash state.
                    try:
                        ws.send('FLASH_' + current_command.upper())
                    except Exception:
                        break
        finally:
            with state_lock:
                if camera_ws is ws:
                    camera_ws = None
    else:
        with state_lock:
            viewer_sockets.add(ws)
        try:
            while True:
                data = ws.receive()
                if data is None:
                    break
        finally:
            with state_lock:
                viewer_sockets.discard(ws)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
