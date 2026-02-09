from flask import Flask, render_template_string, request, jsonify
import serial
import serial.tools.list_ports
import threading
import queue
import time

app = Flask(__name__)

ser = None
connected = False
q = queue.Queue()
cached_ports = []
port_cache_lock = threading.Lock()

def detect_ports():
    global cached_ports
    while True:
        try:
            ports = [port.device for port in serial.tools.list_ports.comports()]
            with port_cache_lock:
                cached_ports = ports
        except Exception as e:
            print(f'[DEBUG] Port detection error: {e}')
        time.sleep(1)

readings = {
    'HOT': '--', 'MID': '--', 'COLD': '--',
    'AIR_T': '--', 'AIR_H': '--', 'LIGHT': '--',
    'RHOT': 'OFF', 'RCOLD': 'OFF'
}
logs = []

HTML = '''
<!DOCTYPE html>
<html>
<head>
    <title>Cloud Chamber Monitor</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 600px; margin: 20px auto; }
        .section { border: 1px solid #ccc; padding: 15px; margin: 10px 0; border-radius: 5px; }
        .section h3 { margin-top: 0; }
        .reading { display: flex; justify-content: space-between; padding: 8px; }
        .reading-label { font-weight: bold; }
        .reading-value { font-family: monospace; }
        button { padding: 10px 15px; margin: 5px; font-size: 14px; cursor: pointer; }
        button:disabled { opacity: 0.5; cursor: not-allowed; }
        input, select { padding: 8px; margin-right: 5px; }
        #status { padding: 10px; text-align: center; font-weight: bold; }
        .connected { color: green; }
        .disconnected { color: red; }
    </style>
    <script>
        function fetchLogs() {
            fetch('/api/logs')
                .then(r => r.json())
                .then(data => {
                    const el = document.getElementById('serialLog');
                    if (!el) return;
                    el.textContent = data.logs.join('\n');
                });
        }

        function loadPorts() {
            fetch('/api/ports')
                .then(r => r.json())
                .then(data => {
                    const select = document.getElementById('portSelect');
                    select.innerHTML = '<option value="">-- Select Port --</option>';
                    data.ports.forEach(port => {
                        const opt = document.createElement('option');
                        opt.value = port;
                        opt.textContent = port;
                        select.appendChild(opt);
                    });
                });
        }
        
        function toggleConnect() {
            const port = document.getElementById('portSelect').value;
            if (!port) { alert('Select a port'); return; }
            fetch('/api/connect', { method: 'POST', headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({port: port})
            }).then(r => r.json()).then(data => updateStatus());
        }
        
        function disconnect() {
            fetch('/api/disconnect', { method: 'POST' })
                .then(r => r.json()).then(data => updateStatus());
        }
        
        function toggleRelay(relay) {
            fetch('/api/relay', { method: 'POST', headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({relay: relay})
            });
        }
        
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('status').className = data.connected ? 'connected' : 'disconnected';
                    document.getElementById('status').textContent = 
                        data.connected ? '✓ Connected to ' + data.port : '✗ Disconnected';
                    document.getElementById('connectBtn').style.display = data.connected ? 'none' : 'inline';
                    document.getElementById('disconnectBtn').style.display = data.connected ? 'inline' : 'none';
                    document.getElementById('portSelect').disabled = data.connected;
                    document.getElementById('hotBtn').disabled = !data.connected;
                    document.getElementById('coldBtn').disabled = !data.connected;
                    
                    Object.keys(data.readings).forEach(k => {
                        const el = document.getElementById('r_' + k);
                        if (el) el.textContent = data.readings[k];
                    });
                });
        }
        
        window.onload = function() {
            loadPorts();
            setInterval(loadPorts, 2000);
            setInterval(updateStatus, 500);
            setInterval(fetchLogs, 500);
        };
    </script>
</head>
<body>
    <h1>Cloud Chamber Monitor</h1>
    
    <div class="section">
        <h3>Connection</h3>
        <select id="portSelect">
            <option value="">-- Loading Ports --</option>
        </select>
        <button id="connectBtn" onclick="toggleConnect()">Connect</button>
        <button id="disconnectBtn" onclick="disconnect()" style="display:none;">Disconnect</button>
    </div>
    
    <div id="status" class="disconnected">✗ Disconnected</div>
    
    <div class="section">
        <h3>Sensor Readings</h3>
        <div class="reading">
            <span class="reading-label">Hot End:</span>
            <span class="reading-value" id="r_HOT">--</span>
        </div>
        <div class="reading">
            <span class="reading-label">Middle:</span>
            <span class="reading-value" id="r_MID">--</span>
        </div>
        <div class="reading">
            <span class="reading-label">Cold End:</span>
            <span class="reading-value" id="r_COLD">--</span>
        </div>
        <div class="reading">
            <span class="reading-label">Air Temperature:</span>
            <span class="reading-value" id="r_AIR_T">--</span>
        </div>
        <div class="reading">
            <span class="reading-label">Air Humidity:</span>
            <span class="reading-value" id="r_AIR_H">--</span>
        </div>
        <div class="reading">
            <span class="reading-label">Light (raw):</span>
            <span class="reading-value" id="r_LIGHT">--</span>
        </div>
    </div>
    
    <div class="section">
        <h3>Relay Control</h3>
        <button id="hotBtn" onclick="toggleRelay('hot')" disabled>Toggle Hot Relay</button>
        <button id="coldBtn" onclick="toggleRelay('cold')" disabled>Toggle Cold Relay</button>
    </div>

    <div class="section">
        <h3>Serial Log</h3>
        <pre id="serialLog" style="height:200px; overflow:auto; background:#111; color:#0f0; padding:10px; border-radius:4px;">--</pre>
    </div>
</body>
</html>
'''

def serial_reader():
    global ser, connected, readings
    print('[DEBUG] serial_reader thread started')
    while connected and ser:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f'[DEBUG] Received: {line}')
                # maintain recent logs for web UI
                try:
                    logs.append(line)
                    if len(logs) > 400:
                        logs.pop(0)
                except Exception:
                    pass
            if line.startswith('SENSORS;'):
                parts = line.split(';')[1:]
                for p in parts:
                    if ':' in p:
                        k, v = p.split(':', 1)
                        # Keep the string value as-is (including "NaN")
                        readings[k] = v
                        print(f'[DEBUG] readings[{k}] = {v}')
        except Exception as e:
            print(f'[DEBUG] serial_reader error: {e}')
        time.sleep(0.02)
    print('[DEBUG] serial_reader ended')

@app.route('/')
def index():
    return render_template_string(HTML)

@app.route('/api/ports')
def api_ports():
    global cached_ports, port_cache_lock
    with port_cache_lock:
        ports = list(cached_ports)
    return jsonify({'ports': ports})

@app.route('/api/connect', methods=['POST'])
def api_connect():
    global ser, connected
    data = request.json
    port = data.get('port')
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(0.2)
        connected = True
        threading.Thread(target=serial_reader, daemon=True).start()
        return jsonify({'ok': True})
    except Exception as e:
        return jsonify({'ok': False, 'error': str(e)})

@app.route('/api/disconnect', methods=['POST'])
def api_disconnect():
    global ser, connected
    connected = False
    try:
        if ser:
            ser.close()
    except Exception:
        pass
    ser = None
    return jsonify({'ok': True})

@app.route('/api/status')
def api_status():
    return jsonify({
        'connected': connected,
        'port': ser.port if ser else None,
        'readings': readings
    })


@app.route('/api/logs')
def api_logs():
    # return the last 400 log lines
    return jsonify({'logs': logs[-400:]})

@app.route('/api/relay', methods=['POST'])
def api_relay():
    data = request.json
    relay = data.get('relay')
    if ser and connected:
        try:
            if relay == 'hot':
                ser.write(b'RELAY HOT TOGGLE\n')
            elif relay == 'cold':
                ser.write(b'RELAY COLD TOGGLE\n')
        except Exception:
            pass
    return jsonify({'ok': True})

if __name__ == '__main__':
    import webbrowser
    import sys
    threading.Thread(target=detect_ports, daemon=True).start()
    port = 8888
    print(f"Starting server at http://localhost:{port}")
    sys.stdout.flush()
    try:
        webbrowser.open(f'http://localhost:{port}')
    except Exception:
        pass
    app.run(host='0.0.0.0', port=port, debug=False, use_reloader=False)
