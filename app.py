from flask import Flask, Response, render_template_string, request
import time

app = Flask(__name__)

PAGE = '''<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32-CAM Global Stream</title>
<style>body{background:#111;color:#fff;font-family:Arial;text-align:center;margin:0;padding:16px}img{max-width:100%;width:640px;border:2px solid #fff;border-radius:8px}button{padding:12px 18px;margin:6px;border:0;border-radius:8px;font-weight:bold}</style></head>
<body><h2>ESP32-CAM Global Stream</h2><img src="/video"><div><button onclick="fetch('/flash/on')">Flash ON</button><button onclick="fetch('/flash/off')">Flash OFF</button></div></body></html>'''

@app.get('/')
def index():
    return render_template_string(PAGE)

@app.post('/frame')
def frame():
    data = request.get_data()
    if not data:
        return ('empty', 400)
    app.config['latest_frame'] = data
    return ('ok', 200)

@app.get('/video')
def video():
    def generate():
        last = None
        while True:
            frame = app.config.get('latest_frame')
            if frame is not None and frame is not last:
                last = frame
                yield b'--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ' + str(len(frame)).encode() + b'\r\n\r\n' + frame + b'\r\n'
            else:
                time.sleep(0.02)
    return Response(generate(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.get('/flash/on')
def flash_on():
    # Placeholder: ESP32 flash control should be implemented on the device-side protocol.
    return ('Flash ON request received', 200)

@app.get('/flash/off')
def flash_off():
    # Placeholder: ESP32 flash control should be implemented on the device-side protocol.
    return ('Flash OFF request received', 200)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
