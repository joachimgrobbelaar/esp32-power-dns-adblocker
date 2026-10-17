#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ==========================================
// Network & Hardware Configuration
// ==========================================
const char* WIFI_SSID     = "NM45_2.4GHz";
const char* WIFI_PASSWORD = "M9NMjkMD";

// Local Machine (Host PC) Target Information
const char* TARGET_PC_IP  = "192.168.0.50";
const int   TARGET_PC_PWR_PORT = 8999;
const uint8_t TARGET_PC_MAC[6] = {0x30, 0x9C, 0x23, 0x9A, 0xB5, 0xFA};

// DNS Configuration
const unsigned int DNS_PORT = 53;
IPAddress UPSTREAM_DNS_PRIMARY(1, 1, 1, 1);    // Cloudflare DNS
IPAddress UPSTREAM_DNS_SECONDARY(8, 8, 8, 8);  // Google DNS
IPAddress BROADCAST_IP(192, 168, 0, 255);
IPAddress GLOBAL_BROADCAST(255, 255, 255, 255);

// Web & Services
WebServer server(80);
WiFiUDP dnsServer;
Preferences prefs;

// ==========================================
// Runtime State & Statistics
// ==========================================
bool adblockEnabled = true;
bool hostPcOnline = false;
unsigned long lastPcPingCheck = 0;
const unsigned long PC_PING_INTERVAL = 4000;

unsigned long totalQueries = 0;
unsigned long blockedQueries = 0;

// Query Log Entry
struct QueryLog {
  char domain[64];
  char clientIp[16];
  bool blocked;
  uint32_t timestamp;
};

const int MAX_LOGS = 25;
QueryLog queryLogs[MAX_LOGS];
int logIndex = 0;
int totalLogsCount = 0;

void addQueryLog(const String& domain, const IPAddress& clientIp, bool blocked) {
  QueryLog& entry = queryLogs[logIndex];
  strncpy(entry.domain, domain.c_str(), sizeof(entry.domain) - 1);
  entry.domain[sizeof(entry.domain) - 1] = '\0';
  
  String ipStr = clientIp.toString();
  strncpy(entry.clientIp, ipStr.c_str(), sizeof(entry.clientIp) - 1);
  entry.clientIp[sizeof(entry.clientIp) - 1] = '\0';
  
  entry.blocked = blocked;
  entry.timestamp = millis() / 1000;
  
  logIndex = (logIndex + 1) % MAX_LOGS;
  if (totalLogsCount < MAX_LOGS) totalLogsCount++;
}

// ==========================================
// Built-in AdBlock Domain List
// ==========================================
const char* const BUILTIN_BLOCKLIST[] = {
  // Google / DoubleClick
  "doubleclick.net",
  "googleads.g.doubleclick.net",
  "pagead2.googlesyndication.com",
  "googlesyndication.com",
  "googleadservices.com",
  "adservice.google.com",
  "google-analytics.com",
  "googletagmanager.com",
  
  // Facebook / Meta
  "pixel.facebook.com",
  "an.facebook.com",
  "analytics.facebook.com",
  
  // Microsoft & Windows Telemetry
  "telemetry.microsoft.com",
  "v10.events.data.microsoft.com",
  "watson.telemetry.microsoft.com",
  
  // Major Ad Exchanges & Networks
  "adnxs.com",
  "criteo.com",
  "taboola.com",
  "outbrain.com",
  "rubiconproject.com",
  "scorecardresearch.com",
  "popads.net",
  "amazon-adsystem.com",
  "media.net",
  "pubmatic.com",
  "openx.net",
  "adroll.com",
  "smartadserver.com",
  "mgid.com",
  "revcontent.com",
  "zergnet.com",
  "bidswitch.net",
  "casalemedia.com",
  "yieldmo.com",
  "advertising.com",
  "samsungads.com",
  "ads.tiktok.com",
  
  // Mobile / Gaming Ads
  "unityads.unity3d.com",
  "applovin.com",
  "vungle.com",
  "ironsrc.mobi",
  "adcolony.com",
  "chartboost.com",
  "flurry.com",
  
  // Analytics & Trackers
  "hotjar.com",
  "segment.io",
  "mixpanel.com",
  "adjust.com",
  "appsflyer.com",
  "branch.io",
  "quantserve.com",
  "moatads.com"
};
const size_t BUILTIN_COUNT = sizeof(BUILTIN_BLOCKLIST) / sizeof(BUILTIN_BLOCKLIST[0]);

// Dynamic Custom Blocklist (up to 32 additional domains saved to NVS)
const int MAX_CUSTOM_DOMAINS = 32;
String customBlocklist[MAX_CUSTOM_DOMAINS];
int customCount = 0;

void loadCustomBlocklist() {
  prefs.begin("adblock", true);
  customCount = prefs.getInt("count", 0);
  if (customCount > MAX_CUSTOM_DOMAINS) customCount = MAX_CUSTOM_DOMAINS;
  for (int i = 0; i < customCount; i++) {
    String key = "d" + String(i);
    customBlocklist[i] = prefs.getString(key.c_str(), "");
  }
  adblockEnabled = prefs.getBool("enabled", true);
  prefs.end();
}

void saveCustomBlocklist() {
  prefs.begin("adblock", false);
  prefs.putInt("count", customCount);
  for (int i = 0; i < customCount; i++) {
    String key = "d" + String(i);
    prefs.putString(key.c_str(), customBlocklist[i]);
  }
  prefs.putBool("enabled", adblockEnabled);
  prefs.end();
}

bool isDomainBlocked(const String& domain) {
  if (!adblockEnabled || domain.length() == 0) return false;

  // 1. Check built-in blocklist (exact match or subdomain)
  for (size_t i = 0; i < BUILTIN_COUNT; i++) {
    const char* blocked = BUILTIN_BLOCKLIST[i];
    if (domain == blocked || domain.endsWith("." + String(blocked))) {
      return true;
    }
  }

  // 2. Check custom user blocklist
  for (int i = 0; i < customCount; i++) {
    if (customBlocklist[i].length() > 0) {
      if (domain == customBlocklist[i] || domain.endsWith("." + customBlocklist[i])) {
        return true;
      }
    }
  }

  // 3. Heuristic ad keywords in domain
  if (domain.indexOf("googleadservices") >= 0 ||
      domain.indexOf("doubleclick") >= 0 ||
      domain.indexOf("googlesyndication") >= 0 ||
      domain.indexOf("adservice") >= 0 ||
      domain.indexOf("adsystem") >= 0 ||
      domain.indexOf("adserver") >= 0 ||
      domain.startsWith("ads.") ||
      domain.indexOf(".ads.") >= 0 ||
      domain.startsWith("telemetry.") ||
      domain.indexOf(".telemetry.") >= 0) {
    return true;
  }

  return false;
}

// ==========================================
// Function 1: PC Power Control Logic
// ==========================================
void sendWakeOnLan() {
  Serial.println("[WoL] Generating Wake-on-LAN Magic Packet for Host PC...");
  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (int i = 0; i < 16; i++) {
    memcpy(packet + 6 + i * 6, TARGET_PC_MAC, 6);
  }

  WiFiUDP udp;
  // Send to subnet broadcast on port 9
  udp.beginPacket(BROADCAST_IP, 9);
  udp.write(packet, sizeof(packet));
  udp.endPacket();

  // Send to subnet broadcast on port 7
  udp.beginPacket(BROADCAST_IP, 7);
  udp.write(packet, sizeof(packet));
  udp.endPacket();

  // Send to global broadcast 255.255.255.255
  udp.beginPacket(GLOBAL_BROADCAST, 9);
  udp.write(packet, sizeof(packet));
  udp.endPacket();

  Serial.println("[WoL] Magic Packet sent successfully!");
}

bool sendPowerOff() {
  Serial.println("[PowerOff] Sending shutdown command to Host PC daemon...");
  HTTPClient http;
  String url = "http://" + String(TARGET_PC_IP) + ":" + String(TARGET_PC_PWR_PORT) + "/api/poweroff";
  http.begin(url);
  http.setTimeout(2500);
  int httpCode = http.POST("{}");
  http.end();
  Serial.printf("[PowerOff] Host response code: %d\n", httpCode);
  return (httpCode == 200 || httpCode == 204);
}

void checkHostPcStatus() {
  WiFiClient client;
  client.setTimeout(1);
  if (client.connect(TARGET_PC_IP, TARGET_PC_PWR_PORT)) {
    hostPcOnline = true;
    client.stop();
  } else {
    // Check fallback port (SSH port 22)
    if (client.connect(TARGET_PC_IP, 22)) {
      hostPcOnline = true;
      client.stop();
    } else {
      hostPcOnline = false;
    }
  }
}

// ==========================================
// Function 2: DNS Sinkhole Implementation
// ==========================================
uint8_t dnsQueryBuf[1024];
uint8_t dnsRespBuf[1024];

String parseDnsDomain(uint8_t* buf, int len, int* qtypeOffset) {
  String domain = "";
  int ptr = 12; // Start of Question Section
  while (ptr < len && buf[ptr] != 0) {
    int labelLen = buf[ptr++];
    if (labelLen > 63 || ptr + labelLen > len) return "";
    for (int i = 0; i < labelLen; i++) {
      domain += (char)buf[ptr++];
    }
    if (ptr < len && buf[ptr] != 0) domain += ".";
  }
  if (ptr < len && buf[ptr] == 0) ptr++;
  if (qtypeOffset) *qtypeOffset = ptr;
  domain.toLowerCase();
  return domain;
}

void handleDnsPacket() {
  int packetSize = dnsServer.parsePacket();
  if (packetSize < 12 || packetSize > (int)sizeof(dnsQueryBuf)) return;

  IPAddress clientIP = dnsServer.remoteIP();
  uint16_t clientPort = dnsServer.remotePort();
  dnsServer.read(dnsQueryBuf, sizeof(dnsQueryBuf));

  totalQueries++;

  int qtypeOffset = 0;
  String domain = parseDnsDomain(dnsQueryBuf, packetSize, &qtypeOffset);

  if (domain.length() == 0) return;

  bool blocked = isDomainBlocked(domain);
  if (blocked) {
    blockedQueries++;
    addQueryLog(domain, clientIP, true);
    Serial.printf("[DNS BLOCKED] %s from %s\n", domain.c_str(), clientIP.toString().c_str());

    // Read Query Type (A, AAAA, HTTPS, etc.)
    uint16_t qtype = 1;
    if (qtypeOffset > 0 && (qtypeOffset + 2) <= packetSize) {
      qtype = (dnsQueryBuf[qtypeOffset] << 8) | dnsQueryBuf[qtypeOffset + 1];
    }

    // Build RFC-compliant DNS response
    memcpy(dnsRespBuf, dnsQueryBuf, packetSize);
    dnsRespBuf[2] = 0x81; // Response, recursion desired
    dnsRespBuf[3] = 0x80; // Recursion available, no error
    dnsRespBuf[8] = 0x00; // NSCOUNT = 0
    dnsRespBuf[9] = 0x00;
    dnsRespBuf[10] = 0x00; // ARCOUNT = 0
    dnsRespBuf[11] = 0x00;

    int idx = (qtypeOffset > 0) ? (qtypeOffset + 4) : packetSize;

    if (qtype == 28) {
      // Type AAAA (IPv6) -> return :: (16 bytes 0x00)
      dnsRespBuf[6] = 0x00; dnsRespBuf[7] = 0x01; // ANCOUNT = 1
      if (idx + 28 < (int)sizeof(dnsRespBuf)) {
        dnsRespBuf[idx++] = 0xC0; dnsRespBuf[idx++] = 0x0C; // Name pointer
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x1C; // Type AAAA
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x01; // Class IN
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x00;
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x3C; // TTL = 60s
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x10; // RDLength = 16
        memset(&dnsRespBuf[idx], 0, 16);                    // ::
        idx += 16;
      }
    } else if (qtype == 1) {
      // Type A (IPv4) -> return 0.0.0.0 (4 bytes 0x00)
      dnsRespBuf[6] = 0x00; dnsRespBuf[7] = 0x01; // ANCOUNT = 1
      if (idx + 16 < (int)sizeof(dnsRespBuf)) {
        dnsRespBuf[idx++] = 0xC0; dnsRespBuf[idx++] = 0x0C; // Name pointer
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x01; // Type A
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x01; // Class IN
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x00;
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x3C; // TTL = 60s
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x04; // RDLength = 4
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x00; // 0.0.0.0
        dnsRespBuf[idx++] = 0x00; dnsRespBuf[idx++] = 0x00;
      }
    } else {
      // Other types (e.g. HTTPS Type 65) -> return NOERROR NODATA
      dnsRespBuf[6] = 0x00; dnsRespBuf[7] = 0x00; // ANCOUNT = 0
    }

    dnsServer.beginPacket(clientIP, clientPort);
    dnsServer.write(dnsRespBuf, idx);
    dnsServer.endPacket();
  } else {
    addQueryLog(domain, clientIP, false);
    // Forward query to upstream DNS resolver
    WiFiUDP forwarder;
    forwarder.begin(0);
    forwarder.beginPacket(UPSTREAM_DNS_PRIMARY, 53);
    forwarder.write(dnsQueryBuf, packetSize);
    forwarder.endPacket();

    unsigned long start = millis();
    bool answered = false;
    while (millis() - start < 350) {
      int respLen = forwarder.parsePacket();
      if (respLen > 0 && respLen <= (int)sizeof(dnsRespBuf)) {
        forwarder.read(dnsRespBuf, sizeof(dnsRespBuf));
        dnsServer.beginPacket(clientIP, clientPort);
        dnsServer.write(dnsRespBuf, respLen);
        dnsServer.endPacket();
        answered = true;
        break;
      }
      delayMicroseconds(500);
    }

    if (!answered) {
      // Fallback to secondary upstream resolver
      forwarder.beginPacket(UPSTREAM_DNS_SECONDARY, 53);
      forwarder.write(dnsQueryBuf, packetSize);
      forwarder.endPacket();
      start = millis();
      while (millis() - start < 350) {
        int respLen = forwarder.parsePacket();
        if (respLen > 0 && respLen <= (int)sizeof(dnsRespBuf)) {
          forwarder.read(dnsRespBuf, sizeof(dnsRespBuf));
          dnsServer.beginPacket(clientIP, clientPort);
          dnsServer.write(dnsRespBuf, respLen);
          dnsServer.endPacket();
          break;
        }
        delayMicroseconds(500);
      }
    }
    forwarder.stop();
  }
}

// ==========================================
// Web Server Frontend & API Endpoints
// ==========================================
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 DevKit V4 // Power Hub & DNS Sinkhole</title>
<style>
:root{--bg:#080c14;--card:#0f172a;--border:#1e293b;--cyan:#00e5ff;--green:#10b981;--red:#ef4444;--text:#f8fafc;--muted:#94a3b8;}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,monospace;}
body{background:var(--bg);color:var(--text);padding:20px;line-height:1.5;}
.container{max-width:1080px;margin:0 auto;}
header{display:flex;justify-content:space-between;align-items:center;padding:16px 24px;background:var(--card);border:1px solid var(--border);border-radius:12px;margin-bottom:20px;}
.logo{font-size:1.25rem;font-weight:700;color:var(--cyan);letter-spacing:1px;}
.badge{padding:4px 12px;border-radius:20px;font-size:0.75rem;font-weight:700;text-transform:uppercase;}
.badge-online{background:rgba(16,185,129,0.15);color:var(--green);border:1px solid var(--green);}
.badge-offline{background:rgba(239,68,68,0.15);color:var(--red);border:1px solid var(--red);}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:20px;margin-bottom:20px;}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px;}
.card h2{font-size:1rem;color:var(--cyan);margin-bottom:14px;display:flex;align-items:center;gap:8px;}
.stat-val{font-size:2rem;font-weight:800;color:var(--text);margin-bottom:4px;}
.stat-sub{font-size:0.8rem;color:var(--muted);}
.btn{background:var(--cyan);color:#080c14;border:none;padding:12px 20px;border-radius:8px;font-weight:700;cursor:pointer;transition:0.2s;width:100%;font-size:0.95rem;margin-top:10px;}
.btn:hover{filter:brightness(1.15);}
.btn-red{background:var(--red);color:#fff;}
.btn-red:hover{filter:brightness(1.15);}
.btn-group{display:flex;gap:12px;}
.toggle-wrapper{display:flex;align-items:center;justify-content:space-between;padding:12px;background:rgba(255,255,255,0.03);border-radius:8px;}
.switch{position:relative;display:inline-block;width:48px;height:24px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#334155;transition:.3s;border-radius:24px;}
.slider:before{position:absolute;content:"";height:18px;width:18px;left:3px;bottom:3px;background-color:white;transition:.3s;border-radius:50%;}
input:checked + .slider{background-color:var(--cyan);}
input:checked + .slider:before{transform:translateX(24px);}
table{width:100%;border-collapse:collapse;font-size:0.82rem;}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--border);}
th{color:var(--muted);font-weight:600;}
.status-tag{padding:2px 8px;border-radius:4px;font-weight:700;font-size:0.7rem;}
.status-blocked{background:rgba(239,68,68,0.2);color:var(--red);}
.status-allowed{background:rgba(16,185,129,0.2);color:var(--green);}
input[type="text"]{background:#080c14;border:1px solid var(--border);color:#fff;padding:8px 12px;border-radius:6px;width:70%;font-family:inherit;}
.custom-form{display:flex;gap:8px;margin-top:12px;}
.custom-tags{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px;}
.tag{background:#1e293b;color:#cbd5e1;padding:4px 8px;border-radius:4px;font-size:0.75rem;display:flex;align-items:center;gap:6px;}
.tag span{cursor:pointer;color:var(--red);font-weight:bold;}
.guide{background:rgba(0,229,255,0.03);border:1px solid rgba(0,229,255,0.2);padding:14px;border-radius:8px;font-size:0.85rem;color:#cbd5e1;margin-top:16px;}
</style>
</head>
<body>
<div class="container">
  <header>
    <div>
      <div class="logo">⚡ ESP32 DEVKIT V4</div>
      <div style="font-size:0.8rem;color:var(--muted);">PC Power Control & DNS Sinkhole AdBlocker</div>
    </div>
    <div id="pcBadge" class="badge badge-offline">Host PC: Checking...</div>
  </header>

  <div class="grid">
    <!-- Function 1: Host Power Control -->
    <div class="card">
      <h2>⚡ Local Machine Power Hub</h2>
      <div style="font-size:0.85rem;color:var(--muted);margin-bottom:10px;">
        Target PC: <b>192.168.0.50</b> (MAC: <code>30:9c:23:9a:b5:fa</code>)
      </div>
      <div class="btn-group">
        <button class="btn" onclick="sendPower('on')">⚡ Power On (WoL)</button>
        <button class="btn btn-red" onclick="sendPower('off')">🛑 Power Off (Shutdown)</button>
      </div>
      <div id="pwrMsg" style="margin-top:10px;font-size:0.8rem;color:var(--cyan);height:20px;"></div>
    </div>

    <!-- Function 2: AdBlock Statistics -->
    <div class="card">
      <h2>🛡️ DNS Sinkhole Metrics</h2>
      <div style="display:flex;justify-content:space-between;text-align:center;">
        <div>
          <div class="stat-val" id="statBlocked" style="color:var(--red);">0</div>
          <div class="stat-sub">Ads Blocked</div>
        </div>
        <div>
          <div class="stat-val" id="statTotal">0</div>
          <div class="stat-sub">Total Queries</div>
        </div>
        <div>
          <div class="stat-val" id="statRate" style="color:var(--cyan);">0%</div>
          <div class="stat-sub">Block Rate</div>
        </div>
      </div>
      <div class="toggle-wrapper" style="margin-top:16px;">
        <span>Master AdBlocker Toggle</span>
        <label class="switch">
          <input type="checkbox" id="adblockToggle" onchange="toggleAdblock(this.checked)">
          <span class="slider"></span>
        </label>
      </div>
    </div>
  </div>

  <!-- Custom Domain Blocklist Card -->
  <div class="card" style="margin-bottom:20px;">
    <h2>🚫 Custom Blocklist Manager</h2>
    <div style="font-size:0.85rem;color:var(--muted);">
      Total active rules: <b id="ruleCount" style="color:var(--cyan);">0</b> (Built-in + Custom domains)
    </div>
    <div class="custom-form">
      <input type="text" id="domainInput" placeholder="e.g. tracking-network.com">
      <button class="btn" style="margin-top:0;width:auto;" onclick="addCustomDomain()">Add Domain</button>
    </div>
    <div class="custom-tags" id="customTags"></div>
  </div>

  <!-- Live DNS Stream Table -->
  <div class="card">
    <h2>📜 Real-Time Network DNS Queries</h2>
    <table>
      <thead>
        <tr>
          <th>Time</th>
          <th>Client IP</th>
          <th>Requested Domain</th>
          <th>Status</th>
        </tr>
      </thead>
      <tbody id="logTableBody">
        <tr><td colspan="4" style="text-align:center;color:var(--muted);">Listening for DNS traffic on port 53...</td></tr>
      </tbody>
    </table>

    <div class="guide">
      <b>💡 How to activate Network-wide AdBlocking:</b><br>
      Point your router's primary DNS or your device's DNS settings to this ESP32's IP address (<code id="espIp">...</code>). All ad queries, banners, trackers, and telemetry will automatically be sinkholed to <code>0.0.0.0</code>!
    </div>
  </div>
</div>

<script>
async function updateStatus() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    document.getElementById('statBlocked').innerText = data.blocked;
    document.getElementById('statTotal').innerText = data.total;
    const rate = data.total > 0 ? ((data.blocked / data.total) * 100).toFixed(1) : 0;
    document.getElementById('statRate').innerText = rate + '%';
    document.getElementById('ruleCount').innerText = data.builtin_rules + data.custom.length;
    document.getElementById('adblockToggle').checked = data.enabled;
    document.getElementById('espIp').innerText = data.esp_ip;

    const badge = document.getElementById('pcBadge');
    if (data.pc_online) {
      badge.className = 'badge badge-online';
      badge.innerText = 'Host PC: ONLINE';
    } else {
      badge.className = 'badge badge-offline';
      badge.innerText = 'Host PC: OFFLINE';
    }

    // Render custom tags
    const tagsDiv = document.getElementById('customTags');
    tagsDiv.innerHTML = data.custom.map(d => 
      `<div class="tag">${d} <span onclick="removeCustomDomain('${d}')">&times;</span></div>`
    ).join('');

  } catch(e) {}
}

async function updateLogs() {
  try {
    const res = await fetch('/api/logs');
    const logs = await res.json();
    const tbody = document.getElementById('logTableBody');
    if (logs.length === 0) return;
    tbody.innerHTML = logs.map(l => `
      <tr>
        <td>${l.time}s</td>
        <td>${l.ip}</td>
        <td><b>${l.domain}</b></td>
        <td><span class="status-tag ${l.blocked ? 'status-blocked' : 'status-allowed'}">${l.blocked ? 'BLOCKED' : 'RESOLVED'}</span></td>
      </tr>
    `).join('');
  } catch(e) {}
}

async function sendPower(action) {
  const pwrMsg = document.getElementById('pwrMsg');
  pwrMsg.innerText = action === 'on' ? 'Sending Wake-on-LAN packet...' : 'Sending shutdown command...';
  try {
    const res = await fetch('/api/power/' + action, {method: 'POST'});
    const data = await res.json();
    pwrMsg.innerText = data.message;
    setTimeout(() => { pwrMsg.innerText = ''; }, 4000);
    setTimeout(updateStatus, 1500);
  } catch(e) {
    pwrMsg.innerText = 'Error sending power command';
  }
}

async function toggleAdblock(enabled) {
  await fetch('/api/adblock/toggle?state=' + (enabled ? '1' : '0'), {method: 'POST'});
  updateStatus();
}

async function addCustomDomain() {
  const input = document.getElementById('domainInput');
  const domain = input.value.trim().toLowerCase();
  if (!domain) return;
  await fetch('/api/adblock/add?domain=' + encodeURIComponent(domain), {method: 'POST'});
  input.value = '';
  updateStatus();
}

async function removeCustomDomain(domain) {
  await fetch('/api/adblock/remove?domain=' + encodeURIComponent(domain), {method: 'POST'});
  updateStatus();
}

setInterval(updateStatus, 2500);
setInterval(updateLogs, 2500);
updateStatus();
updateLogs();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", HTML_INDEX);
}

void handleStatusApi() {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"esp_ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"pc_online\":" + String(hostPcOnline ? "true" : "false") + ",";
  json += "\"enabled\":" + String(adblockEnabled ? "true" : "false") + ",";
  json += "\"total\":" + String(totalQueries) + ",";
  json += "\"blocked\":" + String(blockedQueries) + ",";
  json += "\"builtin_rules\":" + String(BUILTIN_COUNT) + ",";
  json += "\"custom\":[";
  for (int i = 0; i < customCount; i++) {
    json += "\"" + customBlocklist[i] + "\"";
    if (i < customCount - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleLogsApi() {
  String json = "[";
  for (int i = 0; i < totalLogsCount; i++) {
    // Read newest first
    int idx = (logIndex - 1 - i + MAX_LOGS) % MAX_LOGS;
    QueryLog& entry = queryLogs[idx];
    json += "{";
    json += "\"domain\":\"" + String(entry.domain) + "\",";
    json += "\"ip\":\"" + String(entry.clientIp) + "\",";
    json += "\"blocked\":" + String(entry.blocked ? "true" : "false") + ",";
    json += "\"time\":" + String(entry.timestamp);
    json += "}";
    if (i < totalLogsCount - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handlePowerOn() {
  sendWakeOnLan();
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Wake-on-LAN magic packet broadcasted!\"}");
}

void handlePowerOff() {
  bool ok = sendPowerOff();
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Shutdown signal sent to host PC!\"}");
  } else {
    server.send(200, "application/json", "{\"status\":\"sent\",\"message\":\"Shutdown packet dispatched to host 192.168.0.50\"}");
  }
}

void handleAdblockToggle() {
  if (server.hasArg("state")) {
    adblockEnabled = (server.arg("state") == "1");
    saveCustomBlocklist();
  }
  server.send(200, "application/json", "{\"status\":\"ok\",\"enabled\":" + String(adblockEnabled ? "true" : "false") + "}");
}

void handleAdblockAdd() {
  if (server.hasArg("domain") && customCount < MAX_CUSTOM_DOMAINS) {
    String d = server.arg("domain");
    d.trim();
    d.toLowerCase();
    if (d.length() > 0) {
      customBlocklist[customCount++] = d;
      saveCustomBlocklist();
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleAdblockRemove() {
  if (server.hasArg("domain")) {
    String d = server.arg("domain");
    for (int i = 0; i < customCount; i++) {
      if (customBlocklist[i] == d) {
        for (int j = i; j < customCount - 1; j++) {
          customBlocklist[j] = customBlocklist[j+1];
        }
        customCount--;
        saveCustomBlocklist();
        break;
      }
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ==========================================
// Setup & Main Loop
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=============================================");
  Serial.println("  ESP32 DevKit V4: PC Power & DNS AdBlocker  ");
  Serial.println("=============================================");

  loadCustomBlocklist();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(300);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Connection timeout, continuing in STA mode...");
  }

  // Setup mDNS
  if (MDNS.begin("esp32")) {
    Serial.println("[mDNS] Responder started: http://esp32.local");
    MDNS.addService("http", "tcp", 80);
  }

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/api/status", handleStatusApi);
  server.on("/api/logs", handleLogsApi);
  server.on("/api/power/on", HTTP_POST, handlePowerOn);
  server.on("/api/power/off", HTTP_POST, handlePowerOff);
  server.on("/api/adblock/toggle", HTTP_POST, handleAdblockToggle);
  server.on("/api/adblock/add", HTTP_POST, handleAdblockAdd);
  server.on("/api/adblock/remove", HTTP_POST, handleAdblockRemove);

  // Enable CORS
  server.enableCORS(true);
  server.begin();
  Serial.println("[Web] HTTP Server active on port 80");

  // DNS Server
  dnsServer.begin(DNS_PORT);
  Serial.println("[DNS] Sinkhole server active on UDP port 53");
}

void loop() {
  // Handle HTTP clients
  server.handleClient();

  // Handle DNS queries
  handleDnsPacket();

  // Periodic Host PC Ping Check
  if (millis() - lastPcPingCheck > PC_PING_INTERVAL) {
    lastPcPingCheck = millis();
    checkHostPcStatus();
  }
}
