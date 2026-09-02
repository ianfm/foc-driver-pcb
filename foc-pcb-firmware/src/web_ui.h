#pragma once

static const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 FOC & DRV8323 Dashboard</title>
  <style>
    :root {
      --bg: #0b1120;
      --card: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --muted: #94a3b8;
      --primary: #3b82f6;
      --primary-hover: #2563eb;
      --success: #10b981;
      --warning: #f59e0b;
      --danger: #ef4444;
      --purple: #a855f7;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 14px; min-height: 100vh; }
    .container { max-width: 1080px; margin: 0 auto; }
    
    /* Header & Navigation */
    header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; padding-bottom: 14px; border-bottom: 1px solid var(--card-border); margin-bottom: 16px; gap: 12px; }
    h1 { font-size: 1.35rem; font-weight: 700; display: flex; align-items: center; gap: 8px; }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--success); display: inline-block; animation: pulse 2s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }
    .badge { padding: 4px 10px; border-radius: 9999px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.05em; }
    .badge-ok { background: rgba(16, 185, 129, 0.15); color: var(--success); border: 1px solid var(--success); }
    .badge-trip { background: rgba(239, 68, 68, 0.15); color: var(--danger); border: 1px solid var(--danger); }
    .badge-warn { background: rgba(245, 158, 11, 0.15); color: var(--warning); border: 1px solid var(--warning); }

    /* Nav Tabs & Stream Control */
    .nav-bar { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; margin-bottom: 18px; border-bottom: 1px solid var(--card-border); padding-bottom: 10px; gap: 10px; }
    .tabs { display: flex; gap: 8px; }
    .tab-btn { background: transparent; color: var(--muted); border: none; padding: 8px 16px; font-size: 0.9rem; font-weight: 600; border-radius: 8px; cursor: pointer; transition: all 0.2s; }
    .tab-btn:hover { color: var(--text); background: rgba(255,255,255,0.05); }
    .tab-btn.active { color: #fff; background: var(--primary); }

    .stream-ctrl { display: flex; align-items: center; gap: 8px; font-size: 0.85rem; }
    .stream-ctrl select { background: #0f172a; border: 1px solid var(--card-border); color: #f8fafc; padding: 6px 10px; border-radius: 6px; font-size: 0.85rem; outline: none; }

    .tab-content { display: none; }
    .tab-content.active { display: block; }

    /* Cards & Grids */
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 14px; margin-bottom: 20px; }
    .card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 16px; }
    .card-title { font-size: 0.75rem; text-transform: uppercase; color: var(--muted); letter-spacing: 0.05em; margin-bottom: 6px; font-weight: 600; }
    .card-value { font-size: 1.8rem; font-weight: 700; font-family: monospace; }
    .unit { font-size: 0.9rem; color: var(--muted); font-weight: 400; margin-left: 2px; }

    /* Telemetry Visuals */
    .controls { display: grid; grid-template-columns: 1fr; gap: 16px; margin-bottom: 20px; }
    @media (min-width: 840px) { .controls { grid-template-columns: 2fr 1fr; } }
    
    .slider-group { margin-bottom: 16px; }
    .slider-header { display: flex; justify-content: space-between; margin-bottom: 6px; font-size: 0.9rem; }
    .slider-val { font-weight: 700; font-family: monospace; color: var(--primary); }
    input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #334155; outline: none; -webkit-appearance: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; border-radius: 50%; background: var(--primary); cursor: pointer; }
    
    .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
    button { padding: 8px 14px; border-radius: 8px; font-weight: 600; font-size: 0.85rem; border: none; cursor: pointer; transition: all 0.15s ease; }
    .btn-preset { background: #334155; color: var(--text); }
    .btn-preset:hover { background: #475569; }
    .btn-danger { background: var(--danger); color: white; width: 100%; padding: 14px; font-size: 1.05rem; text-transform: uppercase; letter-spacing: 0.05em; }
    .btn-danger:hover { background: #dc2626; }
    .btn-success { background: var(--success); color: white; width: 100%; padding: 12px; margin-top: 10px; }
    .btn-success:hover { background: #059669; }
    .btn-primary { background: var(--primary); color: white; }
    .btn-primary:hover { background: var(--primary-hover); }

    .dial-container { display: flex; flex-direction: column; align-items: center; justify-content: center; }

    /* Register Inspector UI */
    .reg-grid { display: grid; grid-template-columns: 1fr; gap: 16px; }
    @media (min-width: 900px) { .reg-grid { grid-template-columns: 1fr 1fr; } }
    
    .reg-card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 16px; }
    .reg-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 10px; margin-bottom: 12px; }
    .reg-addr { font-family: monospace; background: rgba(59, 130, 246, 0.15); color: #60a5fa; padding: 2px 6px; border-radius: 4px; font-size: 0.8rem; font-weight: 700; }
    .reg-raw { font-family: monospace; font-size: 0.9rem; color: var(--muted); }
    .reg-raw span { color: #f8fafc; font-weight: 700; }

    .bit-chips { display: flex; flex-wrap: wrap; gap: 6px; margin: 10px 0; }
    .bit-chip { padding: 4px 8px; border-radius: 6px; font-size: 0.75rem; font-family: monospace; font-weight: 600; border: 1px solid #334155; background: #0f172a; color: var(--muted); }
    .bit-chip.active-fault { background: rgba(239, 68, 68, 0.2); color: #f87171; border-color: var(--danger); }
    .bit-chip.active-ok { background: rgba(16, 185, 129, 0.15); color: #34d399; border-color: var(--success); }

    .field-row { display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid rgba(255,255,255,0.05); font-size: 0.85rem; }
    .field-row:last-child { border-bottom: none; }
    .field-name { color: var(--muted); }
    .field-ctrl select { background: #0f172a; border: 1px solid var(--card-border); color: #f8fafc; padding: 6px 10px; border-radius: 6px; font-size: 0.85rem; outline: none; }
    .field-ctrl select:focus { border-color: var(--primary); }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div>
        <h1><span class="dot"></span> ESP32-S3 FOC & DRV8323 Dashboard</h1>
        <p style="color: var(--muted); font-size: 0.85rem; margin-top: 2px;">Core 1: Deterministic 5 kHz FOC Loop | Core 0: Control & Web Services</p>
      </div>
      <div id="statusBadge" class="badge badge-ok">System Ready</div>
    </header>

    <!-- Navigation & Telemetry Stream Rate Controls -->
    <div class="nav-bar">
      <div class="tabs">
        <button class="tab-btn active" onclick="showTab('tab-control')">&#127918; Control & Parameters</button>
        <button class="tab-btn" onclick="showTab('tab-regs')">&#9881;&#65039; DRV8323 Register Inspector</button>
      </div>
      <div class="stream-ctrl">
        <button class="btn-preset" style="padding: 6px 10px;" onclick="fetchStatusSnapshot()">&#8635; Refresh Status</button>
        <span style="color: var(--muted); font-size: 0.8rem;">Telemetry Stream:</span>
        <select id="sel_stream_rate" onchange="updateStreamRate()">
          <option value="0">OFF (0% overhead)</option>
          <option value="1000">1 Hz (Low)</option>
          <option value="200">5 Hz (Medium)</option>
          <option value="100" selected>10 Hz (Live Tracking)</option>
        </select>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- TAB 1: CONTROLLER & PARAMETERS -->
    <!-- ================================================================= -->
    <div id="tab-control" class="tab-content active">
      
      <!-- Telemetry Cards -->
      <div class="grid">
        <div class="card">
          <div class="card-title">Motor Current (RMS)</div>
          <div class="card-value" style="color: var(--success);"><span id="txtCurrent">0.00</span><span class="unit">A</span></div>
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
          <div class="card-value" style="color: var(--purple);"><span id="txtAngle">0.0</span><span class="unit">&deg;</span></div>
        </div>
      </div>

      <!-- Controls & Angle Dial -->
      <div class="controls">
        
        <!-- Sliders Card -->
        <div class="card">
          <h2 style="font-size: 1.05rem; margin-bottom: 16px;">Critical Control Parameters</h2>
          
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
              <span>Max Continuous Current Limit</span>
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
              <span>Emergency Shutdown Trip Cutoff</span>
              <span class="slider-val" style="color: var(--danger);"><span id="lblTrip">1.50</span> A</span>
            </div>
            <input type="range" id="rngTrip" min="0.5" max="15.0" step="0.1" value="1.5">
          </div>

        </div>

        <!-- Dial & Action Card -->
        <div class="card" style="display: flex; flex-direction: column; justify-content: space-between;">
          <div class="dial-container">
            <canvas id="angleCanvas" width="160" height="160"></canvas>
            <div style="font-size: 0.8rem; color: var(--muted); margin-top: 6px;">Live Rotor Angle (Mechanical)</div>
          </div>
          <div style="margin-top: 16px; display: flex; flex-direction: column; gap: 8px;">
            <button id="btnToggleEnable" class="btn-primary" style="padding: 12px; font-weight: 700; font-size: 0.95rem;" onclick="toggleMotorEnable()">&#9654; ENABLE MOTOR</button>
            <button class="btn-danger" style="padding: 10px; font-weight: 600;" onclick="emergencyStop()">&#9888; EMERGENCY STOP (COAST)</button>
            <button class="btn-success" style="padding: 8px; font-weight: 600;" onclick="resetDriver()">&#8635; Clear Trip & Reset</button>
          </div>
        </div>

      </div>

      <!-- Encoder Calibration & Standing Offset Card -->
      <div class="card" style="margin-top: 16px;">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
          <h2 style="font-size: 1.05rem; font-weight: 700;">Encoder Alignment & Standing Offset</h2>
          <span style="font-size: 0.8rem; color: var(--muted);">Raw: <span id="lblRawCounts" style="color: var(--primary); font-weight: 600;">0</span> counts</span>
        </div>
        
        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 10px; margin-bottom: 14px;">
          <button class="btn-preset" style="padding: 10px; font-weight: 600;" onclick="setZeroOrientation(0)">&#127919; Set Current as 0&deg; (UP)</button>
          <button class="btn-preset" style="padding: 10px; font-weight: 600;" onclick="setZeroOrientation(180)">&#11015;&#65039; Set Current as 180&deg; (DOWN)</button>
          <button class="btn-preset" style="padding: 10px; font-weight: 600;" onclick="setZeroOrientation(90)">&#10145;&#65039; Set Current as 90&deg; (RIGHT)</button>
          <button class="btn-preset" style="padding: 10px; font-weight: 600;" onclick="setZeroOrientation(270)">&#11013;&#65039; Set Current as 270&deg; (LEFT)</button>
        </div>

        <!-- Fine Tuning Offset Slider -->
        <div class="slider-group" style="margin-bottom: 8px;">
          <div class="slider-header">
            <span>Standing Offset Fine Tuning</span>
            <span class="slider-val"><span id="lblOffsetDeg">0.0</span>&deg; (<span id="lblOffsetCounts">0</span> counts)</span>
          </div>
          <input type="range" id="rngOffset" min="0.0" max="359.9" step="0.5" value="0.0">
        </div>

        <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 10px; font-size: 0.85rem;">
          <label style="display: flex; align-items: center; gap: 8px; cursor: pointer; color: var(--text);">
            <input type="checkbox" id="chkInvert" onchange="toggleInvert(this.checked)" style="cursor: pointer;">
            <span>Reverse Rotation Direction (CW / CCW)</span>
          </label>
          <button class="btn-preset" style="padding: 4px 8px; font-size: 0.75rem;" onclick="resetOffset()">Reset to Raw 0&deg;</button>
        </div>
      </div>

    </div>

    <!-- ================================================================= -->
    <!-- TAB 2: DRV8323 REGISTER INSPECTOR -->
    <!-- ================================================================= -->
    <div id="tab-regs" class="tab-content">
      
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
        <div>
          <h2 style="font-size: 1.15rem; font-weight: 700;">DRV8323 Silicon Register Map (On-Demand SPI)</h2>
          <p style="color: var(--muted); font-size: 0.85rem;">Zero runtime overhead &bull; Registers read/written strictly on user action.</p>
        </div>
        <button class="btn-primary" onclick="readAllRegisters()">&#8635; Read Registers from DRV8323</button>
      </div>

      <div class="reg-grid">
        
        <!-- Reg 0x00: Fault Status 1 (RO) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x00</span>
              <strong style="margin-left: 6px;">Fault Status 1 (RO)</strong>
            </div>
            <div class="reg-raw">RAW: <span id="r0_raw">0x0000</span></div>
          </div>
          <div class="card-title">Active Silicon Fault Flags</div>
          <div class="bit-chips" id="r0_chips">
            <span class="bit-chip" id="chip_FAULT">FAULT</span>
            <span class="bit-chip" id="chip_VDS_OCP">VDS_OCP</span>
            <span class="bit-chip" id="chip_GDF">GDF</span>
            <span class="bit-chip" id="chip_UVLO">UVLO</span>
            <span class="bit-chip" id="chip_OTSD">OTSD</span>
            <span class="bit-chip" id="chip_VCP_UV">VCP_UV</span>
            <span class="bit-chip" id="chip_VDS_HA">VDS_HA</span>
            <span class="bit-chip" id="chip_VDS_LA">VDS_LA</span>
            <span class="bit-chip" id="chip_VDS_HB">VDS_HB</span>
            <span class="bit-chip" id="chip_VDS_LB">VDS_LB</span>
            <span class="bit-chip" id="chip_VDS_HC">VDS_HC</span>
            <span class="bit-chip" id="chip_VDS_LC">VDS_LC</span>
          </div>
        </div>

        <!-- Reg 0x01: VGS Status 2 (RO) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x01</span>
              <strong style="margin-left: 6px;">VGS Status 2 (RO)</strong>
            </div>
            <div class="reg-raw">RAW: <span id="r1_raw">0x0000</span></div>
          </div>
          <div class="card-title">Sense Amp & Gate Drive Faults</div>
          <div class="bit-chips" id="r1_chips">
            <span class="bit-chip" id="chip_SA_OC">SA_OC</span>
            <span class="bit-chip" id="chip_SB_OC">SB_OC</span>
            <span class="bit-chip" id="chip_SC_OC">SC_OC</span>
            <span class="bit-chip" id="chip_OTW">OTW (Warn)</span>
            <span class="bit-chip" id="chip_CPUV">CPUV</span>
            <span class="bit-chip" id="chip_GDF_HA">GDF_HA</span>
            <span class="bit-chip" id="chip_GDF_LA">GDF_LA</span>
            <span class="bit-chip" id="chip_GDF_HB">GDF_HB</span>
            <span class="bit-chip" id="chip_GDF_LB">GDF_LB</span>
            <span class="bit-chip" id="chip_GDF_HC">GDF_HC</span>
            <span class="bit-chip" id="chip_GDF_LC">GDF_LC</span>
          </div>
        </div>

        <!-- Reg 0x05: OCP & Dead-Time Control (RW) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x05</span>
              <strong style="margin-left: 6px;">OCP & Dead-Time Control</strong>
            </div>
            <div class="reg-raw">RAW: <span id="r5_raw">0x0000</span></div>
          </div>
          <div class="field-row">
            <span class="field-name">Dead Time (DEAD_TIME)</span>
            <div class="field-ctrl">
              <select id="sel_DEAD_TIME">
                <option value="0">50 ns</option>
                <option value="1">100 ns</option>
                <option value="2" selected>200 ns (Active)</option>
                <option value="3">400 ns</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">VDS OCP Level (VDS_LVL)</span>
            <div class="field-ctrl">
              <select id="sel_VDS_LVL">
                <option value="0" selected>0.06 V (Most Sensitive)</option>
                <option value="1">0.07 V</option>
                <option value="2">0.09 V</option>
                <option value="3">0.11 V</option>
                <option value="4">0.13 V</option>
                <option value="5">0.17 V</option>
                <option value="6">0.21 V</option>
                <option value="7">0.26 V</option>
                <option value="8">0.31 V</option>
                <option value="9">0.36 V</option>
                <option value="10">0.43 V</option>
                <option value="11">0.52 V</option>
                <option value="12">0.63 V</option>
                <option value="13">0.75 V</option>
                <option value="14">0.89 V</option>
                <option value="15">1.07 V</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">OCP Shutdown Mode</span>
            <div class="field-ctrl">
              <select id="sel_OCP_MODE">
                <option value="0" selected>Latched Fault (Safest)</option>
                <option value="1">Automatic Retry (4ms)</option>
                <option value="2">Report Only</option>
                <option value="3">Disabled</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">OCP Deglitch Filter</span>
            <div class="field-ctrl">
              <select id="sel_OCP_DEG">
                <option value="0">1.0 us</option>
                <option value="1" selected>2.0 us (Standard)</option>
                <option value="2">4.0 us</option>
                <option value="3">8.0 us</option>
              </select>
            </div>
          </div>
          <button class="btn-primary" style="width: 100%; margin-top: 12px;" onclick="writeReg(0x05, buildReg05())">&#10003; Write Reg 0x05 to DRV8323</button>
        </div>

        <!-- Reg 0x03 & 0x04: Gate Drive HS & LS (RW) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x03 & 0x04</span>
              <strong style="margin-left: 6px;">Gate Drive Slew & Peak Current</strong>
            </div>
            <div class="reg-raw">HS: <span id="r3_raw">0x0000</span> | LS: <span id="r4_raw">0x0000</span></div>
          </div>
          <div class="field-row">
            <span class="field-name">HS Peak Source Current (IDRIVEP_HS)</span>
            <div class="field-ctrl">
              <select id="sel_IDRIVEP_HS">
                <option value="0">10 mA</option>
                <option value="1">30 mA</option>
                <option value="2">60 mA</option>
                <option value="3">80 mA</option>
                <option value="4" selected>120 mA (Active)</option>
                <option value="5">140 mA</option>
                <option value="6">170 mA</option>
                <option value="7">190 mA</option>
                <option value="15">1000 mA (Max)</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">HS Peak Sink Current (IDRIVEN_HS)</span>
            <div class="field-ctrl">
              <select id="sel_IDRIVEN_HS">
                <option value="0">20 mA</option>
                <option value="1">60 mA</option>
                <option value="2">120 mA</option>
                <option value="3">160 mA</option>
                <option value="4" selected>240 mA (Active)</option>
                <option value="15">2000 mA (Max)</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">Peak Drive Time (TDRIVE)</span>
            <div class="field-ctrl">
              <select id="sel_TDRIVE">
                <option value="0">500 ns</option>
                <option value="1">1000 ns</option>
                <option value="2" selected>2000 ns (Standard)</option>
                <option value="3">4000 ns</option>
              </select>
            </div>
          </div>
          <button class="btn-primary" style="width: 100%; margin-top: 12px;" onclick="writeGateDriveRegs()">&#10003; Write Reg 0x03 & 0x04 to DRV8323</button>
        </div>

        <!-- Reg 0x06: Current Sense Amplifier (CSA) Control (RW) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x06</span>
              <strong style="margin-left: 6px;">Current Sense Amp (CSA)</strong>
            </div>
            <div class="reg-raw">RAW: <span id="r6_raw">0x0000</span></div>
          </div>
          <div class="field-row">
            <span class="field-name">CSA Gain (CSA_GAIN)</span>
            <div class="field-ctrl">
              <select id="sel_CSA_GAIN">
                <option value="0">5 V/V (&plusmn;33 A range)</option>
                <option value="1" selected>10 V/V (&plusmn;16.5 A range - Active)</option>
                <option value="2">20 V/V (&plusmn;8.25 A range)</option>
                <option value="3">40 V/V (&plusmn;4.12 A range)</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">Midpoint Bias (VREF_DIV)</span>
            <div class="field-ctrl">
              <select id="sel_VREF_DIV">
                <option value="0" selected>VREF/2 (1.65V Bidirectional AC)</option>
                <option value="1">Unidirectional (0V ground ref)</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">Shunt OCP Level (SEN_LVL)</span>
            <div class="field-ctrl">
              <select id="sel_SEN_LVL">
                <option value="0">0.25 V (25 A short trip)</option>
                <option value="1">0.50 V (50 A short trip)</option>
                <option value="2">0.75 V (75 A short trip)</option>
                <option value="3" selected>1.00 V (100 A short trip)</option>
              </select>
            </div>
          </div>
          <button class="btn-primary" style="width: 100%; margin-top: 12px;" onclick="writeReg(0x06, buildReg06())">&#10003; Write Reg 0x06 to DRV8323</button>
        </div>

        <!-- Reg 0x02: Driver Control (RW) -->
        <div class="reg-card">
          <div class="reg-header">
            <div>
              <span class="reg-addr">REG 0x02</span>
              <strong style="margin-left: 6px;">Driver Control & PWM Mode</strong>
            </div>
            <div class="reg-raw">RAW: <span id="r2_raw">0x0000</span></div>
          </div>
          <div class="field-row">
            <span class="field-name">PWM Input Mode (PWM_MODE)</span>
            <div class="field-ctrl">
              <select id="sel_PWM_MODE">
                <option value="0">6x PWM Mode</option>
                <option value="1" selected>3x PWM Mode (Active Complementary)</option>
                <option value="2">1x PWM Mode</option>
                <option value="3">Independent Mode</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">Overtemperature Report (OTW_REP)</span>
            <div class="field-ctrl">
              <select id="sel_OTW_REP">
                <option value="1" selected>Report on nFAULT pin</option>
                <option value="0">Disabled</option>
              </select>
            </div>
          </div>
          <div class="field-row">
            <span class="field-name">Charge Pump UVLO (DIS_CPUV)</span>
            <div class="field-ctrl">
              <select id="sel_DIS_CPUV">
                <option value="0" selected>Enabled (Trip on UVLO)</option>
                <option value="1">Disabled</option>
              </select>
            </div>
          </div>
          <div class="btn-row" style="margin-top: 12px;">
            <button class="btn-primary" style="flex: 1;" onclick="writeReg(0x02, buildReg02())">&#10003; Write Reg 0x02</button>
            <button class="btn-preset" onclick="clearDriverFaultPulse()">&#9889; Clear Fault Pulse (CLR_FLT)</button>
          </div>
        </div>

      </div>

    </div>

  </div>

  <script>
    let streamTimer = null;

    function showTab(tabId) {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      event.target.classList.add('active');
      document.getElementById(tabId).classList.add('active');
      if (tabId === 'tab-regs') {
        readAllRegisters();
      }
    }

    function updateStreamRate() {
      if (streamTimer) {
        clearInterval(streamTimer);
        streamTimer = null;
      }
      const rate = parseInt(document.getElementById('sel_stream_rate').value);
      if (rate > 0) {
        streamTimer = setInterval(fetchStatusSnapshot, rate);
      }
    }

    // Telemetry Controls
    const rngVq = document.getElementById('rngVq');
    const rngLimit = document.getElementById('rngLimit');
    const rngTrip = document.getElementById('rngTrip');
    const canvas = document.getElementById('angleCanvas');
    const ctx = canvas.getContext('2d');
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

    let motorEnabled = false;

    function toggleMotorEnable() {
      motorEnabled = !motorEnabled;
      fetch('/api/set', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: motorEnabled })
      }).then(() => fetchStatusSnapshot()).catch(err => console.error(err));
    }

    function emergencyStop() {
      motorEnabled = false;
      setVq(0.0);
      fetch('/api/stop', { method: 'POST' }).then(() => fetchStatusSnapshot()).catch(err => console.error(err));
    }

    function resetDriver() {
      motorEnabled = false;
      fetch('/api/reset', { method: 'POST' }).then(() => fetchStatusSnapshot()).catch(err => console.error(err));
    }

    function drawDial(angleDeg) {
      const w = canvas.width;
      const h = canvas.height;
      const cx = w / 2;
      const cy = h / 2;
      const r = w / 2 - 16;

      ctx.clearRect(0, 0, w, h);

      // Outer track ring
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, 2 * Math.PI);
      ctx.strokeStyle = '#1e293b';
      ctx.lineWidth = 10;
      ctx.stroke();

      // Cardinal tick labels (0° UP, 90° RIGHT, 180° DOWN, 270° LEFT)
      ctx.fillStyle = '#64748b';
      ctx.font = '10px monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';

      ctx.fillText('0°', cx, cy - r + 9);
      ctx.fillText('180°', cx, cy + r - 9);
      ctx.fillText('90°', cx + r - 10, cy);
      ctx.fillText('270°', cx - r + 10, cy);

      // Needle pointer calculation
      const rad = (angleDeg - 90) * (Math.PI / 180.0);
      const nx = cx + (r - 12) * Math.cos(rad);
      const ny = cy + (r - 12) * Math.sin(rad);

      // Tail counterweight
      const tx = cx - 12 * Math.cos(rad);
      const ty = cy - 12 * Math.sin(rad);

      // Draw needle line
      ctx.beginPath();
      ctx.moveTo(tx, ty);
      ctx.lineTo(nx, ny);
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 4;
      ctx.lineCap = 'round';
      ctx.stroke();

      // Needle pointer dot
      ctx.beginPath();
      ctx.arc(nx, ny, 4, 0, 2 * Math.PI);
      ctx.fillStyle = '#38bdf8';
      ctx.fill();

      // Center pivot hub
      ctx.beginPath();
      ctx.arc(cx, cy, 6, 0, 2 * Math.PI);
      ctx.fillStyle = '#f8fafc';
      ctx.fill();
    }

    let isFetching = false;
    let isOffsetSliding = false;
    const rngOffset = document.getElementById('rngOffset');

    if (rngOffset) {
      rngOffset.addEventListener('input', (e) => {
        document.getElementById('lblOffsetDeg').innerText = parseFloat(e.target.value).toFixed(1);
        isOffsetSliding = true;
      });
      rngOffset.addEventListener('change', (e) => {
        sendParams({ offset_deg: parseFloat(e.target.value) });
        isOffsetSliding = false;
      });
    }

    function setZeroOrientation(targetAngle) {
      sendParams({ set_zero: targetAngle });
      setTimeout(fetchStatusSnapshot, 60);
    }

    function resetOffset() {
      sendParams({ offset_counts: 0 });
      setTimeout(fetchStatusSnapshot, 60);
    }

    function toggleInvert(inverted) {
      sendParams({ inverted: inverted });
      setTimeout(fetchStatusSnapshot, 60);
    }

    function fetchStatusSnapshot() {
      if (isFetching) return;
      isFetching = true;
      fetch('/api/status')
        .then(r => r.json())
        .then(data => {
          isFetching = false;
          document.getElementById('txtCurrent').innerText = data.current.toFixed(2);
          document.getElementById('txtVq').innerText = data.vq.toFixed(2);
          document.getElementById('txtLimit').innerText = data.limit.toFixed(2);
          document.getElementById('txtAngle').innerText = data.angle_deg.toFixed(1);

          if (document.getElementById('lblRawCounts')) {
            document.getElementById('lblRawCounts').innerText = data.raw_angle;
          }
          if (document.getElementById('lblOffsetDeg')) {
            document.getElementById('lblOffsetDeg').innerText = data.offset_deg.toFixed(1);
          }
          if (document.getElementById('lblOffsetCounts')) {
            document.getElementById('lblOffsetCounts').innerText = data.offset_counts;
          }
          if (rngOffset && !isOffsetSliding) {
            rngOffset.value = data.offset_deg;
          }
          if (document.getElementById('chkInvert')) {
            document.getElementById('chkInvert').checked = !!data.inverted;
          }

          motorEnabled = !!data.enabled;
          const btnEnable = document.getElementById('btnToggleEnable');
          if (btnEnable) {
            if (motorEnabled) {
              btnEnable.innerHTML = '&#10074;&#10074; DISABLE MOTOR (COAST)';
              btnEnable.style.background = 'var(--warning)';
            } else {
              btnEnable.innerHTML = '&#9654; ENABLE MOTOR';
              btnEnable.style.background = 'var(--primary)';
            }
          }

          if (!isUserSliding) {
            rngVq.value = data.vq;
            document.getElementById('lblVq').innerText = data.vq.toFixed(2);
          }

          const badge = document.getElementById('statusBadge');
          if (data.tripped) {
            badge.innerText = 'TRIPPED (OVERCURRENT)';
            badge.className = 'badge badge-trip';
          } else if (motorEnabled && !data.hw_ok) {
            badge.innerText = 'HARDWARE FAULT';
            badge.className = 'badge badge-trip';
          } else if (motorEnabled) {
            badge.innerText = 'ACTIVE (RUNNING)';
            badge.className = 'badge badge-ok';
          } else {
            badge.innerText = 'STANDBY (MOTOR UNPOWERED / 0A)';
            badge.className = 'badge badge-warn';
          }

          drawDial(data.angle_deg);
        })
        .catch(err => {
          isFetching = false;
        });
    }

    drawDial(0);
    // Start continuous 10 Hz telemetry stream on page load
    updateStreamRate();
    fetchStatusSnapshot();

    // Register Inspector Functions
    function hex4(v) {
      return '0x' + (v & 0x7FF).toString(16).toUpperCase().padStart(4, '0');
    }

    function updateChip(id, active, isFault) {
      const el = document.getElementById(id);
      if (!el) return;
      if (active) {
        el.className = isFault ? 'bit-chip active-fault' : 'bit-chip active-ok';
      } else {
        el.className = 'bit-chip';
      }
    }

    function readAllRegisters() {
      fetch('/api/drv_regs')
        .then(r => r.json())
        .then(regs => {
          document.getElementById('r0_raw').innerText = hex4(regs.r0);
          document.getElementById('r1_raw').innerText = hex4(regs.r1);
          document.getElementById('r2_raw').innerText = hex4(regs.r2);
          document.getElementById('r3_raw').innerText = hex4(regs.r3);
          document.getElementById('r4_raw').innerText = hex4(regs.r4);
          document.getElementById('r5_raw').innerText = hex4(regs.r5);
          document.getElementById('r6_raw').innerText = hex4(regs.r6);

          // Decode Reg 0x00
          updateChip('chip_FAULT', regs.r0 & (1 << 10), true);
          updateChip('chip_VDS_OCP', regs.r0 & (1 << 9), true);
          updateChip('chip_GDF', regs.r0 & (1 << 8), true);
          updateChip('chip_UVLO', regs.r0 & (1 << 7), true);
          updateChip('chip_OTSD', regs.r0 & (1 << 6), true);
          updateChip('chip_VCP_UV', regs.r0 & (1 << 5), true);
          updateChip('chip_VDS_HA', regs.r0 & (1 << 4), true);
          updateChip('chip_VDS_LA', regs.r0 & (1 << 3), true);
          updateChip('chip_VDS_HB', regs.r0 & (1 << 2), true);
          updateChip('chip_VDS_LB', regs.r0 & (1 << 1), true);
          updateChip('chip_VDS_HC', regs.r0 & (1 << 0), true);

          // Decode Reg 0x01
          updateChip('chip_SA_OC', regs.r1 & (1 << 10), true);
          updateChip('chip_SB_OC', regs.r1 & (1 << 9), true);
          updateChip('chip_SC_OC', regs.r1 & (1 << 8), true);
          updateChip('chip_OTW', regs.r1 & (1 << 7), true);
          updateChip('chip_CPUV', regs.r1 & (1 << 6), true);
          updateChip('chip_GDF_HA', regs.r1 & (1 << 5), true);
          updateChip('chip_GDF_LA', regs.r1 & (1 << 4), true);
          updateChip('chip_GDF_HB', regs.r1 & (1 << 3), true);
          updateChip('chip_GDF_LB', regs.r1 & (1 << 2), true);
          updateChip('chip_GDF_HC', regs.r1 & (1 << 1), true);
          updateChip('chip_GDF_LC', regs.r1 & (1 << 0), true);

          // Sync selectors with read values
          document.getElementById('sel_DEAD_TIME').value = (regs.r5 >> 8) & 0x03;
          document.getElementById('sel_OCP_MODE').value = (regs.r5 >> 6) & 0x03;
          document.getElementById('sel_OCP_DEG').value = (regs.r5 >> 4) & 0x03;
          document.getElementById('sel_VDS_LVL').value = regs.r5 & 0x0F;

          document.getElementById('sel_IDRIVEP_HS').value = (regs.r3 >> 4) & 0x0F;
          document.getElementById('sel_IDRIVEN_HS').value = regs.r3 & 0x0F;
          document.getElementById('sel_TDRIVE').value = (regs.r4 >> 8) & 0x03;

          document.getElementById('sel_CSA_GAIN').value = (regs.r6 >> 6) & 0x03;
          document.getElementById('sel_VREF_DIV').value = (regs.r6 >> 9) & 0x01;
          document.getElementById('sel_SEN_LVL').value = (regs.r6 >> 0) & 0x03;

          document.getElementById('sel_PWM_MODE').value = (regs.r2 >> 5) & 0x07;
          document.getElementById('sel_OTW_REP').value = (regs.r2 >> 8) & 0x01;
          document.getElementById('sel_DIS_CPUV').value = (regs.r2 >> 9) & 0x01;
        })
        .catch(err => console.error('Error fetching DRV8323 regs:', err));
    }

    function buildReg05() {
      const dt = parseInt(document.getElementById('sel_DEAD_TIME').value);
      const mode = parseInt(document.getElementById('sel_OCP_MODE').value);
      const deg = parseInt(document.getElementById('sel_OCP_DEG').value);
      const lvl = parseInt(document.getElementById('sel_VDS_LVL').value);
      return (dt << 8) | (mode << 6) | (deg << 4) | lvl;
    }

    function buildReg06() {
      const gain = parseInt(document.getElementById('sel_CSA_GAIN').value);
      const vref = parseInt(document.getElementById('sel_VREF_DIV').value);
      const sen = parseInt(document.getElementById('sel_SEN_LVL').value);
      return (vref << 9) | (gain << 6) | sen;
    }

    function buildReg02() {
      const pwm = parseInt(document.getElementById('sel_PWM_MODE').value);
      const otw = parseInt(document.getElementById('sel_OTW_REP').value);
      const cpuv = parseInt(document.getElementById('sel_DIS_CPUV').value);
      return (cpuv << 9) | (otw << 8) | (pwm << 5);
    }

    function writeGateDriveRegs() {
      const idrivep = parseInt(document.getElementById('sel_IDRIVEP_HS').value);
      const idriven = parseInt(document.getElementById('sel_IDRIVEN_HS').value);
      const tdrive = parseInt(document.getElementById('sel_TDRIVE').value);

      // Reg 0x03: Unlock (011b) + HS drive
      const r3 = (0x03 << 8) | (idrivep << 4) | idriven;
      // Reg 0x04: TDRIVE + LS drive
      const r4 = (tdrive << 8) | (idrivep << 4) | idriven;

      writeReg(0x03, r3).then(() => writeReg(0x04, r4));
    }

    function clearDriverFaultPulse() {
      const r2 = buildReg02() | 0x01; // Set CLR_FLT bit 0
      writeReg(0x02, r2);
    }

    function writeReg(addr, val) {
      return fetch('/api/drv_write', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ addr: addr, val: val })
      })
      .then(r => r.json())
      .then(() => {
        setTimeout(readAllRegisters, 100);
      })
      .catch(err => console.error('Write failed:', err));
    }
  </script>
</body>
</html>
)rawliteral";
