from flask import Flask, Response, render_template_string, request
import time
import threading

app = Flask(__name__)

frame_lock = threading.Lock()
latest_frame = None
flash_command = "off"
last_frame_time = 0

PAGE = '''<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Global Stream</title>
<style>
body{background:#111;color:#fff;font-family:Arial;text-align:center;margin:0;padding:16px}
img{max-width:100%;width:640px;border:2px solid #fff;border-radius:8px;background:#000}
button{padding:12px 18px;margin:6px;border:0;border-radius:8px;font-weight:bold}
#status{margin:10px}
</style>
</head>
<body>
<h2>ESP32-CAM Global Stream</h2>
<img src="/video">
<div>
<button onclick="flash('/flash/on')">🔦 Flash ON</button>
<button onclick="flash('/flash/off')">💡 Flash OFF</button>
</div>
<div id="status">Connecting...</div>
<script>
function flash(url){fetch(url).then(r=>r.text()).then(t=>document.getElementById('status').textContent=t).catch(()=>{});}
setInterval(()=>fetch('/status').then(r=>r.json()).then(s=>{
 document.getElementById('status').textContent = s.online ? 'Camera online' : 'Waiting for camera...';
}).catch(()=>{}),3000);
</script>
</body>
</html>'''

@app.get('/')
def index():
    return render_template_string(PAGE)

@app.post('/frame')
def frame():
    global latest_frame, last_frame_time
    data = request.get_data()
    if not data:
        return ('empty', 400)
    with frame_lock:
        latest_frame = data
        last_frame_time = time.time()
    return ('ok', 200)

@app.get('/video')
def video():
    def generate():
        sent = None
        while True:
            with frame_lock:
                frame = latest_frame
            if frame is not None and frame is not sent:
                sent = frame
                yield (b'--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ' +
                       str(len(frame)).encode() + b'\r\n\r\n' + frame + b'\r\n')
            else:
                time.sleep(0.02)
    return Response(generate(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.get('/flash/on')
def flash_on():
    global flash_command
    flash_command = 'on'
    return ('Flash ON command sent to ESP32-CAM', 200)

@app.get('/flash/off')
def flash_off():
    global flash_command
    flash_command = 'off'
    return ('Flash OFF command sent to ESP32-CAM', 200)

@app.get('/command')
def command():
    return flash_command

@app.get('/status')
def status():
    return {'online': (time.time() - last_frame_time) < 10 if last_frame_time else False}

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
