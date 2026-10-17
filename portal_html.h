#pragma once
#include <Arduino.h>

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<title>ESP32 Command Hub • Start/Restart, Mouse, GameSir BT, Devices & AdBlock</title>
<style>
:root{--bg:#0b0f17;--card:#151c28;--border:#283446;--text:#e2e8f0;--muted:#8b9bb4;--accent:#00f2fe;--accent2:#4facfe;--success:#2ea043;--danger:#f85149;--warning:#d29922;--purple:#a371f7}
*{box-sizing:border-box;margin:0;padding:0;user-select:none;-webkit-user-select:none}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--text);padding:12px 12px 80px 12px;max-width:1020px;margin:0 auto;line-height:1.45}
.header{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--card);border:1px solid var(--border);border-radius:14px;margin-bottom:12px;flex-wrap:wrap;gap:8px}
.header h1{font-size:1.15rem;color:#fff;display:flex;align-items:center;gap:8px}
.badges{display:flex;gap:8px;flex-wrap:wrap}
.badge{padding:4px 10px;border-radius:20px;font-size:0.72rem;font-weight:700;text-transform:uppercase}
.b-on{background:#133922;color:#3fb950;border:1px solid #2ea043}
.b-off{background:#42181d;color:#f85149;border:1px solid #f85149}
.b-bt{background:#1b2c48;color:var(--accent);border:1px solid var(--accent2)}
.tab-view{display:none}
.tab-view.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin-bottom:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:14px;margin-bottom:12px}
.card-title{font-size:0.78rem;text-transform:uppercase;color:var(--muted);font-weight:700;margin-bottom:8px;letter-spacing:0.5px}
.stat-val{font-size:1.5rem;font-weight:800;color:#fff}
.btn-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px}
.btn{padding:12px 14px;border-radius:12px;font-size:0.88rem;font-weight:700;cursor:pointer;border:1px solid var(--border);background:#1e293b;color:#fff;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;text-decoration:none;transition:transform 0.12s,background 0.2s;text-align:center}
.btn:active{transform:scale(0.96)}
.btn span{font-size:0.7rem;color:var(--muted);font-weight:400}
.btn-row{flex-direction:row;gap:8px}
.btn-wake{background:linear-gradient(135deg,#1f6feb,#2ea043);border:none}
.btn-reboot{background:linear-gradient(135deg,#9a6700,#d29922);border:none}
.btn-sleep{background:linear-gradient(135deg,#3b2e58,#6e40c9);border:none}
.btn-off{background:linear-gradient(135deg,#9e1c23,#f85149);border:none}
.btn-accent{background:linear-gradient(135deg,#0072ff,#00c6ff);color:#fff;border:none}
.l-yt{background:linear-gradient(135deg,#5a1414,#c4302b)}
.l-dis{background:linear-gradient(135deg,#0d1b4c,#113ccf)}
.l-pr{background:linear-gradient(135deg,#083c5a,#00a8e1)}
.l-apple{background:linear-gradient(135deg,#1c1c1e,#3a3a3c)}
.l-cast{background:linear-gradient(135deg,#0f3d3e,#14b8a6)}
.l-tv{background:linear-gradient(135deg,#311042,#9333ea)}
.trackpad-wrap{display:flex;gap:10px;height:240px;margin-bottom:10px}
#touchpad{flex:1;background:#0e1520;border:2px dashed #334155;border-radius:16px;display:flex;flex-direction:column;align-items:center;justify-content:center;color:#64748b;touch-action:none}
#scrollpad{width:48px;background:#0e1520;border:1px solid #334155;border-radius:16px;display:flex;align-items:center;justify-content:center;color:#64748b;font-size:1.3rem;touch-action:none}
input[type=text]{width:100%;padding:11px;border-radius:10px;border:1px solid var(--border);background:#0b0f17;color:#fff;font-size:0.95rem}
table{width:100%;border-collapse:collapse;font-size:0.82rem;margin-top:6px}
th,td{padding:8px;text-align:left;border-bottom:1px solid var(--border)}
th{color:var(--muted)}
.dev-card{background:#101722;border:1px solid var(--border);border-radius:12px;padding:12px;margin-bottom:8px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
nav{position:fixed;bottom:0;left:0;right:0;background:rgba(15,23,34,0.96);backdrop-filter:blur(12px);border-top:1px solid var(--border);display:flex;justify-content:space-around;padding:7px 0;z-index:999}
.nav-btn{flex:1;display:flex;flex-direction:column;align-items:center;color:var(--muted);font-size:0.7rem;font-weight:600;cursor:pointer;gap:2px}
.nav-btn.active{color:var(--accent)}
.nav-btn i{font-style:normal;font-size:1.35rem}
#toast{position:fixed;top:16px;right:16px;background:#1e293b;color:#fff;border:1px solid var(--accent);padding:10px 16px;border-radius:10px;font-size:0.85rem;display:none;z-index:2000;box-shadow:0 6px 20px rgba(0,0,0,0.4)}
</style>
</head>
<body>
<div id="toast"></div>

<div class="header">
  <div>
    <h1>🛡️ ESP32 Command & AdBlock Hub</h1>
    <small style="color:var(--muted)">Wi-Fi: <b>AdBlock_WiFi</b> (Pass: <b>adblock123</b>) • LAN: <b>192.168.0.201</b></small>
  </div>
  <div class="badges">
    <span id="pc-badge" class="badge b-on">PC Online</span>
    <span id="bt-badge" class="badge b-bt">BT Hub Ready</span>
  </div>
</div>

<!-- TAB 1: START / RESTART & DESKTOP LAUNCHER -->
<div id="tab-launch" class="tab-view active">
  <div class="card">
    <div class="card-title">⚡ PC Start, Restart, Sleep & Shutdown Portal (192.168.0.50)</div>
    <div class="btn-grid">
      <button class="btn btn-wake" onclick="powerAction('wake')">⚡ Start / Wake PC<span>Hardware WoL Magic Packet</span></button>
      <button class="btn btn-reboot" onclick="powerAction('reboot')">🔄 Restart / Reboot<span>Systemd Graceful Reboot</span></button>
      <button class="btn btn-sleep" onclick="powerAction('sleep')">💤 Sleep / Suspend<span>Low-Power Suspend Mode</span></button>
      <button class="btn btn-off" onclick="powerAction('shutdown')">🛑 Power Off PC<span>Graceful Host Shutdown</span></button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">🖥️ Desktop & Smart TV App Launcher (Launches on Main Display :0)</div>
    <div class="btn-grid">
      <button class="btn l-tv" onclick="launchApp('tvhub')">🖥️ TV Hub Portal<span>Fullscreen Media Deck</span></button>
      <button class="btn l-apple" onclick="launchApp('appletv')">🍎 Apple TV+<span>Fullscreen Web App</span></button>
      <button class="btn l-yt" onclick="launchApp('youtube')">📺 YouTube TV<span>Fullscreen Smart TV UI</span></button>
      <button class="btn l-yt" onclick="launchApp('ytmusic')">🎵 YouTube Music<span>Fullscreen Audio Player</span></button>
      <button class="btn l-dis" onclick="launchApp('disney')">⭐ Disney+<span>Fullscreen Cinema</span></button>
      <button class="btn l-pr" onclick="launchApp('prime')">🍿 Prime Video<span>Fullscreen Streaming</span></button>
      <button class="btn l-cast" onclick="launchApp('phonecast')">📱 Cast Phone Screen<span>Full Mirror (Redmi 13C)</span></button>
      <button class="btn l-tv" onclick="launchApp('p10tv')">📡 P10 Smart TV<span>Open Receiver (:8088/tv)</span></button>
      <button class="btn l-cast" onclick="launchApp('castremote')">🎮 Redmi Cast Remote<span>Controller (:8088/cast)</span></button>
      <button class="btn" onclick="launchApp('magnifier')">🔍 Screen Magnifier<span>Toggle 2x–5x Zoom</span></button>
      <button class="btn" onclick="launchApp('fixaudio')">🔊 Fix HDMI Audio<span>Reset PulseAudio/HDMI</span></button>
      <button class="btn" onclick="launchApp('jellyfin')">🎬 Jellyfin Server<span>Media Library (:8096)</span></button>
    </div>
    <button class="btn btn-off btn-row" style="width:100%;margin-top:10px" onclick="launchApp('close')">❌ Close Active Window (Alt + F4)</button>
  </div>
</div>

<!-- TAB 2: WIRELESS MOUSE, KEYBOARD & MEDIA -->
<div id="tab-mouse" class="tab-view">
  <div class="card">
    <div class="card-title">🖱️ Wireless Mouse Touchpad & Scrollbar</div>
    <div class="trackpad-wrap">
      <div id="touchpad">
        <div style="font-size:2rem;margin-bottom:6px">🖱️</div>
        <div>Slide to Move Cursor • Tap to Click</div>
      </div>
      <div id="scrollpad">↕</div>
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px">
      <button class="btn btn-row" onclick="mouseClick('left')">🖱️ Left</button>
      <button class="btn btn-row" onclick="mouseClick('double')">⚡ Double</button>
      <button class="btn btn-row" onclick="mouseClick('right')">🖱️ Right</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">⏯️ Media, Volume & Quick Keyboard</div>
    <div style="display:flex;gap:8px;margin-bottom:10px">
      <input type="text" id="kb-text" placeholder="Type text to send to PC...">
      <button class="btn btn-accent btn-row" style="padding:0 20px" onclick="sendText()">Send</button>
    </div>
    <div style="display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px">
      <button class="btn btn-row" onclick="mediaCmd('voldown')">🔉 Vol -</button>
      <button class="btn btn-row" onclick="mediaCmd('mute')">🔇 Mute</button>
      <button class="btn btn-row" onclick="mediaCmd('volup')">🔊 Vol +</button>
      <button class="btn btn-row" onclick="mediaCmd('prev')">⏮️ Prev</button>
      <button class="btn btn-accent btn-row" onclick="mediaCmd('play')">⏯️ Play/Pause</button>
      <button class="btn btn-row" onclick="mediaCmd('next')">⏭️ Next</button>
    </div>
    <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:8px">
      <button class="btn btn-row" onclick="sendKey('Escape')">Esc</button>
      <button class="btn btn-row" onclick="sendKey('Up')">▲</button>
      <button class="btn btn-row" onclick="sendKey('BackSpace')">⌫</button>
      <button class="btn btn-row" onclick="sendKey('Return')">Enter</button>
      <button class="btn btn-row" onclick="sendKey('Left')">◀</button>
      <button class="btn btn-row" onclick="sendKey('Down')">▼</button>
      <button class="btn btn-row" onclick="sendKey('Right')">▶</button>
      <button class="btn btn-row" onclick="sendKey('space')">Space</button>
    </div>
  </div>
</div>

<!-- TAB 3: BLUETOOTH GAMESIR CONTROLLER & WIRELESS MOUSE HUB -->
<div id="tab-bt" class="tab-view">
  <div class="card">
    <div class="card-title">🎮 ESP32 Dual-Mode Bluetooth Receiver (GameSir Controller & Wireless Mouse)</div>
    <p style="font-size:0.84rem;color:var(--muted);margin-bottom:10px">
      Pairs your <b>GameSir Bluetooth Controller</b> or <b>Bluetooth Wireless Mouse</b> directly to the ESP32 and bridges inputs to Pop!_OS via kernel <code>UInput</code>.
    </p>
    <div class="grid">
      <div class="card" style="margin:0;background:#101722">
        <div class="card-title">Connected BT Device</div>
        <div id="bt-conn-name" style="font-size:1.1rem;font-weight:700;color:var(--accent)">Scanning / Ready</div>
        <small id="bt-conn-mac" style="color:var(--muted)">Put GameSir or Mouse in Pairing Mode</small>
      </div>
      <div class="card" style="margin:0;background:#101722">
        <div class="card-title">GameSir Stick-to-Mouse Mode</div>
        <div id="bt-mouse-mode" style="font-size:1.1rem;font-weight:700;color:#3fb950">Enabled (Left Stick = Cursor)</div>
        <small style="color:var(--muted)">A=Left Click, B=Right Click, R-Stick=Scroll</small>
      </div>
    </div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:10px">
      <button class="btn btn-accent btn-row" onclick="scanBluetooth()">🔍 Scan for GameSir & Bluetooth Mouse</button>
      <button class="btn btn-row" onclick="toggleGameSirMouseMode()">🔄 Toggle Stick-to-Mouse Mode</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">📡 Discovered Bluetooth Devices (Click Connect to Pair)</div>
    <div id="bt-list">
      <div style="color:var(--muted);font-size:0.85rem">Click "Scan for GameSir & Bluetooth Mouse" while your controller/mouse LED is blinking rapidly.</div>
    </div>
  </div>

  <div class="card">
    <div class="card-title">🕹️ On-Screen Virtual GameSir Controller Pad (Also Bridges Physical HTML5 Gamepads)</div>
    <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:8px">
      <button class="btn btn-row" onclick="sendGamepadBtn(0)">🅰️ Btn A (Click)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(1)">🅱️ Btn B (Right)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(2)">🇽 Btn X (Enter)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(3)">🇾 Btn Y (Esc)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(4)">L1 (Vol -)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(5)">R1 (Vol +)</button>
      <button class="btn btn-row" onclick="sendGamepadBtn(6)">Select</button>
      <button class="btn btn-accent btn-row" onclick="sendGamepadBtn(7)">Start (TV Hub)</button>
    </div>
  </div>
</div>

<!-- TAB 4: ALL WI-FI DEVICES & ANDROID PHONE HUB -->
<div id="tab-devices" class="tab-view">
  <div class="card">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;flex-wrap:wrap;gap:8px">
      <div>
        <div class="card-title" style="margin:0">🌐 All Connected Wi-Fi Devices (Main LAN 192.168.0.x ↔ AdBlock_WiFi 192.168.4.x)</div>
        <small style="color:var(--muted)">Cross-subnet routing enabled: all devices & Android phones can access each other.</small>
      </div>
      <button class="btn btn-accent btn-row" style="padding:8px 14px" onclick="loadNetworkDevices(true)">🔄 Rescan Network</button>
    </div>
    <div id="network-devices-list">Loading network devices...</div>
  </div>

  <div class="card">
    <div class="card-title">📂 Universal Cross-Device Services & File Sharing</div>
    <div class="btn-grid">
      <a class="btn" href="http://192.168.0.50:8080" target="_blank">📱 Pop!_OS Remote & Video Drop<span>http://192.168.0.50:8080</span></a>
      <a class="btn" href="http://192.168.0.50:8088/cast" target="_blank">📡 Redmi 13C Cast Controller<span>http://192.168.0.50:8088/cast</span></a>
      <a class="btn" href="http://192.168.0.50:8088/tv" target="_blank">📺 P10 Smart TV Receiver<span>http://192.168.0.50:8088/tv</span></a>
      <a class="btn" href="ftp://192.168.0.50:2121" target="_blank">📁 Direct FTP File Server<span>ftp://192.168.0.50:2121</span></a>
    </div>
  </div>
</div>

<!-- TAB 5: ADBLOCK DNS SINKHOLE -->
<div id="tab-adblock" class="tab-view">
  <div class="grid">
    <div class="card" style="margin:0"><div class="card-title">Blocked Domains</div><div id="st-domains" class="stat-val" style="color:var(--accent)">103,681</div><small style="color:var(--muted)">Flash Partition 0x290000</small></div>
    <div class="card" style="margin:0"><div class="card-title">Total Queries</div><div id="st-queries" class="stat-val">0</div><small style="color:var(--muted)">UDP Port 53</small></div>
    <div class="card" style="margin:0"><div class="card-title">Ads Sinkholed</div><div id="st-blocked" class="stat-val" style="color:var(--danger)">0</div><small style="color:var(--muted)">0.0.0.0 / ::</small></div>
    <div class="card" style="margin:0"><div class="card-title">AdBlock_WiFi Clients</div><div id="st-ap" class="stat-val" style="color:#3fb950">0</div><small style="color:var(--muted)">192.168.4.x NAT</small></div>
  </div>

  <div class="card">
    <div class="card-title">🔍 Test Domain & AdBlock Engine Control</div>
    <div style="display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap">
      <a href="/api/adblock/toggle" class="btn btn-row">🛡️ Toggle AdBlock On/Off</a>
      <a href="/api/status" target="_blank" class="btn btn-row">📊 Raw Status JSON</a>
    </div>
    <form action="/test" method="GET" style="display:flex;gap:8px">
      <input type="text" name="q" placeholder="Test domain (e.g. doubleclick.net or ads.tiktok.com)" required>
      <button type="submit" class="btn btn-accent btn-row" style="padding:0 18px">Test</button>
    </form>
  </div>

  <div class="card">
    <div class="card-title">📜 Recent DNS Queries</div>
    <div id="dns-log-wrap">Loading query log...</div>
  </div>
</div>

<nav>
  <div class="nav-btn active" onclick="showTab('launch',0)"><i>🚀</i><span>Start & Apps</span></div>
  <div class="nav-btn" onclick="showTab('mouse',1)"><i>🖱️</i><span>Mouse & KB</span></div>
  <div class="nav-btn" onclick="showTab('bt',2)"><i>🎮</i><span>GameSir BT</span></div>
  <div class="nav-btn" onclick="showTab('devices',3)"><i>🌐</i><span>All Devices</span></div>
  <div class="nav-btn" onclick="showTab('adblock',4)"><i>🛡️</i><span>AdBlock</span></div>
</nav>

<script>
const PC_API = "http://192.168.0.50:8999";

function toast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.style.display = 'block';
  clearTimeout(window._tTimer);
  window._tTimer = setTimeout(() => t.style.display = 'none', 2600);
}

function showTab(id, idx) {
  document.querySelectorAll('.tab-view').forEach(e => e.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(e => e.classList.remove('active'));
  document.getElementById('tab-' + id).classList.add('active');
  document.querySelectorAll('.nav-btn')[idx].classList.add('active');
  if (id === 'devices') loadNetworkDevices(false);
  if (id === 'bt') refreshBtStatus();
  if (id === 'adblock') refreshEspStatus();
}

async function powerAction(action) {
  if (action === 'wake') {
    fetch('/api/pc/wake').then(() => toast('⚡ Sent Wake-on-LAN Magic Packet to PC!'));
    return;
  }
  if (!confirm('Are you sure you want to ' + action.toUpperCase() + ' the PC?')) return;
  toast('Sending ' + action + ' command...');
  try {
    await fetch(PC_API + '/api/system?action=' + action);
    toast('✅ PC ' + action + ' initiated');
  } catch (e) {
    await fetch('/api/pc/' + action);
    toast('Sent via ESP32 fallback');
  }
}

async function launchApp(app) {
  toast('🚀 Launching ' + app + ' on PC...');
  try {
    await fetch(PC_API + '/api/launch?app=' + app);
  } catch (e) {
    await fetch('/api/pc/launch?app=' + app);
  }
}

const pad = document.getElementById('touchpad');
const spad = document.getElementById('scrollpad');
let lastX = 0, lastY = 0, touching = false, moved = false, touchStart = 0;

pad.addEventListener('touchstart', e => {
  if (e.touches.length === 1) {
    lastX = e.touches[0].clientX;
    lastY = e.touches[0].clientY;
    touching = true;
    moved = false;
    touchStart = Date.now();
  } else if (e.touches.length === 2) {
    moved = true;
    mouseClick('right');
  }
  e.preventDefault();
}, {passive:false});

pad.addEventListener('touchmove', e => {
  if (!touching || e.touches.length !== 1) return;
  const dx = (e.touches[0].clientX - lastX) * 1.8;
  const dy = (e.touches[0].clientY - lastY) * 1.8;
  lastX = e.touches[0].clientX;
  lastY = e.touches[0].clientY;
  if (Math.abs(dx) > 1 || Math.abs(dy) > 1) {
    moved = true;
    fetch(PC_API + '/api/mouse/move?dx=' + dx.toFixed(1) + '&dy=' + dy.toFixed(1)).catch(()=>{});
  }
  e.preventDefault();
}, {passive:false});

pad.addEventListener('touchend', e => {
  if (touching && !moved && (Date.now() - touchStart < 280)) {
    mouseClick('left');
  }
  touching = false;
});

let mDown = false;
pad.addEventListener('mousedown', e => { mDown = true; lastX = e.clientX; lastY = e.clientY; });
window.addEventListener('mouseup', () => mDown = false);
pad.addEventListener('mousemove', e => {
  if (!mDown) return;
  const dx = (e.clientX - lastX) * 1.6;
  const dy = (e.clientY - lastY) * 1.6;
  lastX = e.clientX; lastY = e.clientY;
  fetch(PC_API + '/api/mouse/move?dx=' + dx.toFixed(1) + '&dy=' + dy.toFixed(1)).catch(()=>{});
});

let sLastY = 0;
spad.addEventListener('touchstart', e => { sLastY = e.touches[0].clientY; e.preventDefault(); }, {passive:false});
spad.addEventListener('touchmove', e => {
  const dy = sLastY - e.touches[0].clientY;
  if (Math.abs(dy) > 12) {
    sLastY = e.touches[0].clientY;
    fetch(PC_API + '/api/mouse/scroll?dy=' + (dy > 0 ? -1 : 1)).catch(()=>{});
  }
  e.preventDefault();
}, {passive:false});

function mouseClick(btn) { fetch(PC_API + '/api/mouse/click?button=' + btn).catch(()=>{}); }
function mediaCmd(act) { fetch(PC_API + '/api/media/control?action=' + act).catch(()=>{}); }
function sendKey(k) { fetch(PC_API + '/api/keyboard/key?key=' + encodeURIComponent(k)).catch(()=>{}); }
function sendText() {
  const el = document.getElementById('kb-text');
  if (!el.value) return;
  fetch(PC_API + '/api/keyboard/type?text=' + encodeURIComponent(el.value)).catch(()=>{});
  el.value = '';
}

async function scanBluetooth() {
  toast('🔍 Scanning Bluetooth (BLE + Classic) for GameSir & Mouse...');
  fetch('/api/bt/scan').catch(()=>{});
  fetch(PC_API + '/api/bt/scan').catch(()=>{});
  setTimeout(refreshBtStatus, 2500);
  setTimeout(refreshBtStatus, 5000);
}

async function connectBt(mac, type) {
  toast('🔗 Connecting to ' + mac + '...');
  await fetch('/api/bt/connect?mac=' + encodeURIComponent(mac) + '&type=' + type);
  setTimeout(refreshBtStatus, 2000);
}

async function toggleGameSirMouseMode() {
  try {
    const r = await fetch(PC_API + '/api/bt/mouse_mode');
    const d = await r.json();
    document.getElementById('bt-mouse-mode').innerText = d.gamesir_mouse_mode ? 'Enabled (Left Stick = Cursor)' : 'Disabled (Gamepad Only)';
    toast('GameSir Mouse Mode: ' + (d.gamesir_mouse_mode ? 'ON' : 'OFF'));
  } catch (e) {}
}

async function sendGamepadBtn(bit) {
  const mask = (1 << bit);
  await fetch(PC_API + '/api/gamepad', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({lx:0, ly:0, rx:0, ry:0, buttons:mask, hat:0})
  }).catch(()=>{});
  setTimeout(() => {
    fetch(PC_API + '/api/gamepad', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({lx:0, ly:0, rx:0, ry:0, buttons:0, hat:0})
    }).catch(()=>{});
  }, 120);
}

setInterval(() => {
  const gps = navigator.getGamepads ? navigator.getGamepads() : [];
  for (let i = 0; i < gps.length; i++) {
    const gp = gps[i];
    if (!gp) continue;
    const lx = Math.round((gp.axes[0] || 0) * 127);
    const ly = Math.round((gp.axes[1] || 0) * 127);
    const rx = Math.round((gp.axes[2] || 0) * 127);
    const ry = Math.round((gp.axes[3] || 0) * 127);
    let btns = 0;
    gp.buttons.forEach((b, idx) => { if (b.pressed) btns |= (1 << idx); });
    if (Math.abs(lx) > 12 || Math.abs(ly) > 12 || Math.abs(rx) > 12 || Math.abs(ry) > 12 || btns !== 0) {
      fetch(PC_API + '/api/gamepad', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({lx, ly, rx, ry, buttons:btns, hat:0})
      }).catch(()=>{});
    }
  }
}, 50);

async function refreshBtStatus() {
  try {
    const res = await fetch('/api/bt/status');
    const data = await res.json();
    document.getElementById('bt-conn-name').innerText = data.connected ? data.connected_name : (data.scanning ? 'Scanning...' : 'Ready to Pair');
    document.getElementById('bt-conn-mac').innerText = data.connected ? ('MAC: ' + data.connected_mac + ' • Packets: ' + data.packets) : 'Put GameSir or Mouse in pairing mode';
    const listEl = document.getElementById('bt-list');
    if (data.devices && data.devices.length > 0) {
      let html = '';
      data.devices.forEach(d => {
        html += `<div class="dev-card">
          <div><b>${d.name}</b> <small style="color:var(--accent)">[${d.type}]</small><br><small style="color:var(--muted)">MAC: ${d.mac} • RSSI: ${d.rssi} dBm</small></div>
          <button class="btn btn-accent btn-row" style="padding:8px 14px" onclick="connectBt('${d.mac}',${d.type==='CLASSIC'?1:0})">Connect</button>
        </div>`;
      });
      listEl.innerHTML = html;
    }
  } catch (e) {}
}

async function loadNetworkDevices(force) {
  const wrap = document.getElementById('network-devices-list');
  if (force) wrap.innerHTML = 'Scanning both 192.168.0.x (Main LAN) and 192.168.4.x (AdBlock_WiFi)...';
  try {
    const res = await fetch(PC_API + '/api/network/devices?refresh=' + (force ? '1' : '0'));
    const data = await res.json();
    if (data.devices) {
      let html = '';
      data.devices.forEach(d => {
        let actions = '';
        if (d.type === 'android') {
          actions += `<button class="btn l-cast btn-row" style="padding:7px 12px" onclick="mirrorPhone('${d.ip}')">📱 Mirror Screen</button>`;
          actions += `<button class="btn btn-row" style="padding:7px 12px" onclick="adbConnect('${d.ip}')">🔗 ADB Connect</button>`;
        }
        (d.links || []).forEach(l => {
          actions += `<a class="btn btn-row" style="padding:7px 12px" href="${l.url}" target="_blank">${l.label}</a>`;
        });
        html += `<div class="dev-card">
          <div>
            <div style="font-weight:700;font-size:0.95rem">${d.name}</div>
            <div style="font-size:0.78rem;color:var(--muted)">IP: <b style="color:#fff">${d.ip}</b> • MAC: ${d.mac} • <span style="color:var(--accent)">${d.network}</span></div>
            <div style="font-size:0.74rem;color:#64748b">Open Ports: ${d.services && d.services.length ? d.services.join(', ') : 'Standard'}</div>
          </div>
          <div style="display:flex;gap:6px;flex-wrap:wrap">${actions}</div>
        </div>`;
      });
      wrap.innerHTML = html;
      return;
    }
  } catch (e) {
    try {
      const r2 = await fetch('/api/ap/clients');
      const d2 = await r2.json();
      let html = '<div style="margin-bottom:8px;color:var(--warning)">Host PC is offline/sleeping. Showing AdBlock_WiFi clients directly from ESP32:</div>';
      (d2.clients || []).forEach(c => {
        html += `<div class="dev-card"><div><b>📱 AdBlock_WiFi Client</b><br><small>IP: ${c.ip} • MAC: ${c.mac} • RSSI: ${c.rssi} dBm</small></div></div>`;
      });
      wrap.innerHTML = html;
    } catch (e2) {}
  }
}

async function mirrorPhone(ip) {
  toast('📱 Launching Screen Mirror for ' + ip + '...');
  await fetch(PC_API + '/api/network/mirror?ip=' + encodeURIComponent(ip)).catch(()=>{});
}

async function adbConnect(ip) {
  toast('🔗 Connecting Wireless ADB to ' + ip + '...');
  const r = await fetch(PC_API + '/api/network/adb_connect?ip=' + encodeURIComponent(ip));
  const d = await r.json();
  toast(d.message || 'ADB command sent');
}

async function refreshEspStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    document.getElementById('st-domains').innerText = d.total_domains.toLocaleString();
    document.getElementById('st-queries').innerText = d.total_queries.toLocaleString();
    document.getElementById('st-blocked').innerText = d.blocked_queries.toLocaleString();
    document.getElementById('st-ap').innerText = d.ap_stations;
    const pcBadge = document.getElementById('pc-badge');
    pcBadge.className = 'badge ' + (d.host_pc_online ? 'b-on' : 'b-off');
    pcBadge.innerText = d.host_pc_online ? 'PC Online' : 'PC Offline';
    const btBadge = document.getElementById('bt-badge');
    btBadge.innerText = d.bt_connected ? ('🎮 ' + d.bt_name) : 'BT Hub Ready';

    if (d.logs) {
      let rows = '<table><thead><tr><th>Domain</th><th>Client</th><th>Result</th></tr></thead><tbody>';
      d.logs.forEach(l => {
        rows += `<tr><td><code>${l.domain}</code></td><td>${l.ip}</td><td style="color:${l.blocked?'#f85149':'#3fb950'}">${l.blocked?'🚫 Blocked':'✓ Allowed'}</td></tr>`;
      });
      rows += '</tbody></table>';
      document.getElementById('dns-log-wrap').innerHTML = rows;
    }
  } catch (e) {}
}

setInterval(refreshEspStatus, 3500);
refreshEspStatus();
</script>
</body>
</html>)rawliteral";
