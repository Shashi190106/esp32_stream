import time
from threading import Lock
from flask import Flask, Response, render_template_string
from flask_sock import Sock

app = Flask(__name__)
sock = Sock(app)
frame_lock = Lock()
latest_frame = None
viewers = set()
flash_state = "OFF"

PAGE = '''<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32-CAM Global</title><style>body{font-family:Arial;background:#111;color:#eee;text-align:center;margin:0;padding:12px}img{width:min(96vw,960px);height:auto;background:#000;border-radius:8px}.btn{padding:12px 18px;margin:6px;border:0;border-radius:7px;font-weight:bold}.stat{margin:8px}</style></head><body><h2>ESP32-CAM Global Stream</h2><img id="cam"><div class="stat" id="fps">Connecting...</div><button class="btn" onclick="cmd('ON')">Flash ON</button><button class="btn" onclick="cmd('OFF')">Flash OFF</button><script>const img=document.getElementById('cam'),fps=document.getElementById('fps');const ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/viewer');ws.binaryType='blob';let n=0,t=performance.now();ws.onopen=()=>fps.textContent='Connected';ws.onmessage=e=>{const u=URL.createObjectURL(e.data);const old=img.src;img.src=u;if(old)URL.revokeObjectURL(old);n++;let now=performance.now();if(now-t>1000){fps.textContent='Live viewer FPS: '+(n*1000/(now-t)).toFixed(1);n=0;t=now}};ws.onclose=()=>fps.textContent='Disconnected';function cmd(x){fetch('/flash/'+x.toLowerCase(),{method:'POST'})}</script></body></html>'''

@app.get('/')
def index(): return render_template_string(PAGE)

@app.get('/flash/status')
def flash_status(): return flash_state

@app.post('/flash/on')
def flash_on():
    global flash_state
    flash_state='ON'; return 'Flash ON'

@app.post('/flash/off')
def flash_off():
    global flash_state
    flash_state='OFF'; return 'Flash OFF'

@sock.route('/ws')
def camera(ws):
    while True:
        try:
            data=ws.receive()
            if data is None: break
            if isinstance(data, bytes):
                with frame_lock:
                    global latest_frame
                    latest_frame=data
        except Exception: break

@sock.route('/viewer')
def viewer(ws):
    viewers.add(ws)
    last=None
    try:
        while True:
            with frame_lock: frame=latest_frame
            if frame is not None and frame is not last:
                ws.send(frame); last=frame
            else: time.sleep(0.01)
    except Exception: pass
    finally: viewers.discard(ws)

@app.get('/health')
def health(): return {'ok':True,'camera_connected':latest_frame is not None,'viewers':len(viewers),'flash':flash_state}
