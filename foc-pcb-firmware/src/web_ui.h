#pragma once

static const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 FOC Dashboard</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --muted: #94a3b8;
      --primary: #3b82f6;
      --success: #10b981;
      --warning: #f59e0b;
      --danger: #ef4444;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 16px; min-height: 100vh; }
    .container { max-width: 960px; margin: 0 auto; }
    header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; padding-bottom: 16px; border-bottom: 1px solid var(--card-border); margin-bottom: 20px; gap: 10px; }
    h1 { font-size: 1.4rem; font-weight: 700; display: flex; align-items: center; gap: 8px; }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--success); display: inline-block; animation: pulse 2s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }
    .badge { padding: 4px 10px; border-radius: 9999px; font-size: 0.75rem; font-weight: 600; text-transform: uppercase; }
    .badge-ok { background: rgba(16, 185, 129, 0.2); color: var(--success); border: 1px solid var(--success); }
    .badge-trip { background: rgba(239, 68, 68, 0.2); color: var(--danger); border: 1px solid var(--danger); }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 14px; margin-bottom: 20px; }
    .card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 16px; }
    .card-title { font-size: 0.8rem; text-transform: uppercase; color: var(--muted); letter-spacing: 0.05em; margin-bottom: 6px; }
    .card-value { font-size: 1.8rem; font-weight: 700; font-family: monospace; }
    .unit { font-size: 0.9rem; color: var(--muted); font-weight: 400; margin-left: 2px; }
    
    .controls { display: grid; grid-template-columns: 1fr; gap: 16px; margin-bottom: 20px; }
    @media (min-width: 768px) { .controls { grid-template-columns: 2fr 1fr; } }
    
    .slider-group { margin-bottom: 16px; }
    .slider-header { display: flex; justify-content: space-between; margin-bottom: 6px; font-size: 0.9rem; }
    .slider-val { font-weight: 700; font-family: monospace; color: var(--primary); }
    input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #334155; outline: none; -webkit-appearance: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 22px; height: 22px; border-radius: 50%; background: var(--primary); cursor: pointer; }
    
    .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
    button { padding: 8px 14px; border-radius: 8px; font-weight: 600; font-size: 0.85rem; border: none; cursor: pointer; transition: all 0.15s ease; }
    .btn-preset { background: #334155; color: var(--text); }
    .btn-preset:hover { background: #475569; }
    .btn-danger { background: var(--danger); color: white; width: 100%; padding: 14px; font-size: 1.1rem; text-transform: uppercase; letter-spacing: 0.05em; }
    .btn-danger:hover { background: #dc2626; }
    .btn-success { background: var(--success); color: white; width: 100%; padding: 12px; margin-top: 10px; }
    .btn-success:hover { background: #059669; }
    
    .dial-container { display: flex; flex-direction: column; align-items: center; justify-content: center; }
    #angleCanvas { background: transparent; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div>
        <h1><span class="dot"></span> ESP32-S3 FOC Live Dashboard</h1>
        <p style="color: var(--muted); font-size: 0.85rem; margin-top: 2px;">240 MHz Dual-Core | 25 kHz MCPWM | 5 kHz FOC Loop</p>
      </div>
      <div id="statusBadge" class="badge badge-ok">System Ready</div>
    </header>

    <!-- Telemetry Metric Cards -->
    <div class="grid">
      <div class="card">
        <div class="card-title">Motor Current (RMS)</div>
        <div class="card-value"><span id="txtCurrent">0.00</span><span class="unit">A</span></div>
      </div>
      <div class="card">
        <div class="card-title">Torque Voltage (Vq)</div>
        <div class="card-value" style="color: var(--primary);"><span id="txtVq">0.00</span><span class="unit">V</span></div>
      </div>
      <div class="card">
        <div class="card-title">Current Limit</div>
        <div class="card-value" style="color: var(--warning);"><span id="txtLimit">1.00</span><span class="unit">A</span></div>
      </div>
      <div class="card">
        <div class="card-title">Electrical Angle</div>
        <div class="card-value"><span id="txtAngle">0.0</span><span class="unit">&deg;</span></div>
      </div>
    </div>

    <!-- Main Controls & Visualizer -->
    <div class="controls">
      
      <!-- Sliders Card -->
      <div class="card">
        <h2 style="font-size: 1.1rem; margin-bottom: 16px;">Runtime FOC Parameters</h2>
        
        <!-- Vq Slider -->
        <div class="slider-group">
          <div class="slider-header">
            <span>Torque Voltage Target (Vq)</span>
            <span class="slider-val"><span id="lblVq">0.00</span> V</span>
          </div>
          <input type="range" id="rngVq" min="-12.0" max="12.0" step="0.1" value="0.0">
          <div class="btn-row">
            <button class="btn-preset" onclick="setVq(0.0)">0.0 V (Stop)</button>
            <button class="btn-preset" onclick="setVq(0.5)">+0.5 V</button>
            <button class="btn-preset" onclick="setVq(1.0)">+1.0 V</button>
            <button class="btn-preset" onclick="setVq(2.0)">+2.0 V</button>
            <button class="btn-preset" onclick="setVq(-1.0)">-1.0 V (Rev)</button>
          </div>
        </div>

        <!-- Current Limit Slider -->
        <div class="slider-group">
          <div class="slider-header">
            <span>Max Current Limit</span>
            <span class="slider-val" style="color: var(--warning);"><span id="lblLimit">1.00</span> A</span>
          </div>
          <input type="range" id="rngLimit" min="0.2" max="10.0" step="0.1" value="1.0">
          <div class="btn-row">
            <button class="btn-preset" onclick="setLimit(0.5)">0.5 A</button>
            <button class="btn-preset" onclick="setLimit(1.0)">1.0 A</button>
            <button class="btn-preset" onclick="setLimit(2.0)">2.0 A</button>
            <button class="btn-preset" onclick="setLimit(5.0)">5.0 A</button>
          </div>
        </div>

        <!-- Emergency Trip Slider -->
        <div class="slider-group" style="margin-bottom: 0;">
          <div class="slider-header">
            <span>Emergency Shutdown Trip Current</span>
            <span class="slider-val" style="color: var(--danger);"><span id="lblTrip">1.50</span> A</span>
          </div>
          <input type="range" id="rngTrip" min="0.5" max="15.0" step="0.1" value="1.5">
        </div>

      </div>

      <!-- Actions & Angle Dial Card -->
      <div class="card" style="display: flex; flex-direction: column; justify-content: space-between;">
        
        <div class="dial-container">
          <canvas id="angleCanvas" width="160" height="160"></canvas>
          <div style="font-size: 0.8rem; color: var(--muted); margin-top: 4px;">Rotor Angle Dial</div>
        </div>

        <div style="margin-top: 16px;">
          <button class="btn-danger" onclick="emergencyStop()">&#9888; EMERGENCY STOP</button>
          <button class="btn-success" onclick="resetDriver()">&#8635; Clear Trip / Re-Enable</button>
        </div>

      </div>

    </div>

  </div>

  <script>
    const rngVq = document.getElementById('rngVq');
    const rngLimit = document.getElementById('rngLimit');
    const rngTrip = document.getElementById('rngTrip');
    const canvas = document.getElementById('angleCanvas');
    const ctx = canvas.getContext('2d');

    let currentAngleDeg = 0;
    let isUserSliding = false;

    rngVq.addEventListener('input', (e) => {
      document.getElementById('lblVq').innerText = parseFloat(e.target.value).toFixed(2);
      isUserSliding = true;
    });
    rngVq.addEventListener('change', (e) => {
      sendParams({ vq: parseFloat(e.target.value) });
      isUserSliding = false;
    });

    rngLimit.addEventListener('input', (e) => {
      document.getElementById('lblLimit').innerText = parseFloat(e.target.value).toFixed(2);
    });
    rngLimit.addEventListener('change', (e) => {
      sendParams({ limit: parseFloat(e.target.value) });
    });

    rngTrip.addEventListener('input', (e) => {
      document.getElementById('lblTrip').innerText = parseFloat(e.target.value).toFixed(2);
    });
    rngTrip.addEventListener('change', (e) => {
      sendParams({ trip: parseFloat(e.target.value) });
    });

    function setVq(val) {
      rngVq.value = val;
      document.getElementById('lblVq').innerText = val.toFixed(2);
      sendParams({ vq: val });
    }

    function setLimit(val) {
      rngLimit.value = val;
      document.getElementById('lblLimit').innerText = val.toFixed(2);
      sendParams({ limit: val });
    }

    function sendParams(payload) {
      fetch('/api/set', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).catch(err => console.error(err));
    }

    function emergencyStop() {
      setVq(0.0);
      fetch('/api/stop', { method: 'POST' }).catch(err => console.error(err));
    }

    function resetDriver() {
      fetch('/api/reset', { method: 'POST' }).catch(err => console.error(err));
    }

    function drawDial(angleDeg) {
      const w = canvas.width;
      const h = canvas.height;
      const cx = w / 2;
      const cy = h / 2;
      const r = w / 2 - 12;

      ctx.clearRect(0, 0, w, h);

      // Outer circle
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, 2 * Math.PI);
      ctx.strokeStyle = '#334155';
      ctx.lineWidth = 6;
      ctx.stroke();

      // Pointer needle
      const rad = (angleDeg - 90) * (Math.PI / 180.0);
      const px = cx + (r - 10) * Math.cos(rad);
      const py = cy + (r - 10) * Math.sin(rad);

      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(px, py);
      ctx.strokeStyle = '#3b82f6';
      ctx.lineWidth = 4;
      ctx.lineCap = 'round';
      ctx.stroke();

      // Center dot
      ctx.beginPath();
      ctx.arc(cx, cy, 6, 0, 2 * Math.PI);
      ctx.fillStyle = '#f8fafc';
      ctx.fill();
    }

    // 10 Hz Telemetry Polling Loop
    setInterval(() => {
      fetch('/api/status')
        .then(r => r.json())
        .then(data => {
          document.getElementById('txtCurrent').innerText = data.current.toFixed(2);
          document.getElementById('txtVq').innerText = data.vq.toFixed(2);
          document.getElementById('txtLimit').innerText = data.limit.toFixed(2);
          document.getElementById('txtAngle').innerText = data.angle_deg.toFixed(1);

          if (!isUserSliding) {
            rngVq.value = data.vq;
            document.getElementById('lblVq').innerText = data.vq.toFixed(2);
          }

          const badge = document.getElementById('statusBadge');
          if (data.tripped) {
            badge.innerText = 'TRIPPED';
            badge.className = 'badge badge-trip';
          } else if (!data.hw_ok) {
            badge.innerText = 'HARDWARE FAULT';
            badge.className = 'badge badge-trip';
          } else {
            badge.innerText = 'RUNNING OK';
            badge.className = 'badge badge-ok';
          }

          drawDial(data.angle_deg);
        })
        .catch(err => console.log('Poll disconnected'));
    }, 100);

    drawDial(0);
  </script>
</body>
</html>
)rawliteral";
