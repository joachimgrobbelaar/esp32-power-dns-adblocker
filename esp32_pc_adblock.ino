#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "esp_partition.h"
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>

// ==========================================
// Network & Hardware Configuration
// ==========================================
// Upstream Wi-Fi Router (STA Mode)
const char* WIFI_SSID     = "NM45_2.4GHz";
const char* WIFI_PASSWORD = "M9NMjkMD";

// AdBlock Wi-Fi Access Point (AP Mode + NAT Router)
const char* AP_SSID       = "AdBlock_WiFi";
const char* AP_PASSWORD   = "adblock123";  // Simple WPA2 key for all devices
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

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

// Memory-Mapped Flash Blocklist Table
static esp_partition_mmap_handle_t mmapHandle = 0;
static const uint8_t* mmapPtr = nullptr;
static uint32_t flashDomainCount = 0;
static bool flashBlocklistLoaded = false;

// Query Log Entry
struct QueryLog {
  char domain[64];
  char clientIp[16];
  bool blocked;
  uint32_t timestamp;
};

const int MAX_LOGS = 30;
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
// Built-in AdBlock Fallback Domain List
// ==========================================
const char* const BUILTIN_BLOCKLIST[] = {
  // Google / Display Ads
  "doubleclick.net", "googleads.g.doubleclick.net", "pagead2.googlesyndication.com",
  "googlesyndication.com", "googleadservices.com", "adservice.google.com",
  "google-analytics.com", "googletagmanager.com", "admob.com", "2mdn.net",

  // Video Pre-Rolls & In-Stream Ads
  "imasdk.googleapis.com", "s.youtube.com", "video-ad-stats.googlevideo.com",
  "innovid.com", "spotxchange.com", "spotx.tv", "tremorhub.com", "springserve.com",
  "brightroll.com", "flashtalking.com", "serving-sys.com", "connatix.com", "aniview.com",

  // Mobile Reel & In-App Video Ad SDKs (TikTok, Unity, AppLovin, Mintegral, IronSource)
  "ads.tiktok.com", "analytics.tiktok.com", "business-api.tiktok.com", "log.tiktokv.com",
  "ib.tiktokv.com", "mon.tiktokv.com", "ad.oceanengine.com", "pglstatp-toutiao.com",
  "pangle.io", "pangolin-sdk-toutiao.com", "ads.unity3d.com", "auction.unityads.unity3d.com",
  "webview.unityads.unity3d.com", "a.applovin.com", "ms.applovin.com", "d.applovin.com",
  "rt.applovin.com", "net.mintegral.com", "pgl.mintegral.com", "init.supersonicads.com",
  "outcome.ironsrc.com", "ads.api.vungle.com", "config.vungle.com", "config.inmobi.com",
  "ads.inmobi.com", "live.chartboost.com", "da.chartboost.com", "ads.liftoff.io",
  "engine.fyber.com", "events3alt.adcolony.com", "ads.adcolony.com",

  // Popups, Overlays & Anti-Adblock
  "popads.net", "popcash.net", "propellerads.com", "adcash.com", "exoclick.com",
  "juicyads.com", "ero-advertising.com", "trafficjunky.com", "clickadu.com",
  "admaven.com", "adsterra.com", "hilltopads.com", "zergnet.com", "revcontent.com",
  "adblock-analytics.com", "blockadblock.com", "detectadblock.com", "getadmiral.com",
  "admiral.com", "ad-score.com", "adblocker-detector.com",

  // Paywalls & Membership Interceptors
  "tinypass.com", "piano.io", "api.tinypass.com", "experience.tinypass.com",
  "qiota.com", "evolok.net", "poool.fr",

  // Trackers & Telemetry
  "criteo.com", "criteo.net", "taboola.com", "outbrain.com", "adnxs.com",
  "rubiconproject.com", "pubmatic.com", "openx.net", "casalemedia.com",
  "scorecardresearch.com", "hotjar.com", "fullstory.com", "mouseflow.com",
  "crazyegg.com", "onesignal.com", "braze.com", "branch.io", "adjust.com",
  "appsflyer.com", "singular.net", "kochava.com"
};
const size_t BUILTIN_BLOCKLIST_COUNT = sizeof(BUILTIN_BLOCKLIST) / sizeof(BUILTIN_BLOCKLIST[0]);

// FNV-1a 64-bit hash function (case-insensitive)
static inline uint64_t hashDomain(const char* str, size_t len) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < len; i++) {
    char c = str[i];
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    h ^= (uint64_t)(unsigned char)c;
    h *= 1099511628211ULL;
  }
  return h;
}

// Binary search on memory-mapped flash table
static bool binarySearchFlash(uint64_t hash) {
  if (!flashBlocklistLoaded || mmapPtr == nullptr || flashDomainCount == 0) return false;
  
  const uint64_t* table = (const uint64_t*)(mmapPtr + 12);
  int left = 0;
  int right = (int)flashDomainCount - 1;
  
  while (left <= right) {
    int mid = left + (right - left) / 2;
    uint64_t midVal = table[mid];
    if (midVal == hash) return true;
    if (midVal < hash) left = mid + 1;
    else right = mid - 1;
  }
  return false;
}

// Initialize memory-mapped flash blocklist partition
void initFlashBlocklist() {
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (!part) {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  }
  
  if (part) {
    Serial.printf("[BLOCKLIST] Found partition '%s' at 0x%06x, size %u bytes\n", part->label, part->address, part->size);
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, (const void**)&mmapPtr, &mmapHandle);
    if (err == ESP_OK && mmapPtr != nullptr) {
      uint32_t magic = *(const uint32_t*)mmapPtr;
      uint32_t version = *((const uint32_t*)mmapPtr + 1);
      uint32_t count = *((const uint32_t*)mmapPtr + 2);
      
      if (magic == 0x424C4B4C) { // "BLKL"
        flashDomainCount = count;
        flashBlocklistLoaded = true;
        Serial.printf("[BLOCKLIST] Flash blocklist verified! Loaded %u domains (v%u)\n", count, version);
        return;
      } else {
        Serial.printf("[BLOCKLIST] Partition magic mismatch: 0x%08x (expected 0x424C4B4C)\n", magic);
      }
    } else {
      Serial.printf("[BLOCKLIST] Failed to mmap partition: 0x%x\n", err);
    }
  } else {
    Serial.println("[BLOCKLIST] SPIFFS partition not found!");
  }
  flashBlocklistLoaded = false;
  flashDomainCount = 0;
}

bool isDomainBlocked(const String& rawDomain) {
  if (!adblockEnabled) return false;
  
  String dom = rawDomain;
  dom.trim();
  dom.toLowerCase();
  
  // Whitelist core infrastructure
  if (dom == "router" || dom.endsWith(".local") || dom.endsWith(".lan") || dom == "esp32.local") {
    return false;
  }
  
  // Check exact domain in Flash table
  uint64_t fullHash = hashDomain(dom.c_str(), dom.length());
  if (binarySearchFlash(fullHash)) return true;
  
  // Suffix/Subdomain matching in Flash table (e.g., "ad.doubleclick.net" -> "doubleclick.net")
  const char* str = dom.c_str();
  const char* p = str;
  while ((p = strchr(p, '.')) != NULL) {
    p++; // skip '.'
    if (strchr(p, '.') != NULL) { // ensure not matching TLD alone
      uint64_t subHash = hashDomain(p, strlen(p));
      if (binarySearchFlash(subHash)) return true;
    }
  }
  
  // Check Builtin list fallback
  for (size_t i = 0; i < BUILTIN_BLOCKLIST_COUNT; i++) {
    const char* bDom = BUILTIN_BLOCKLIST[i];
    if (dom == bDom || dom.endsWith(String(".") + bDom)) {
      return true;
    }
  }
  
  return false;
}

// ==========================================
// DNS Protocol Engine (UDP 53)
// ==========================================
struct DNSHeader {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
};

String parseDNSQueryName(const uint8_t* buffer, size_t length, size_t& offset, uint16_t& qtype, uint16_t& qclass) {
  String domain = "";
  offset = 12; // DNS header size
  
  while (offset < length) {
    uint8_t len = buffer[offset++];
    if (len == 0) break;
    
    if ((len & 0xC0) == 0xC0) {
      offset++; // Compressed pointer
      break;
    }
    
    if (offset + len > length) return "";
    
    if (domain.length() > 0) domain += ".";
    for (uint8_t i = 0; i < len; i++) {
      domain += (char)buffer[offset++];
    }
  }
  
  if (offset + 4 <= length) {
    qtype = (buffer[offset] << 8) | buffer[offset + 1];
    qclass = (buffer[offset + 2] << 8) | buffer[offset + 3];
    offset += 4;
  }
  
  return domain;
}

size_t makeSinkholeResponse(const uint8_t* queryBuf, size_t queryLen, size_t qnameEndOffset, uint16_t qtype, uint8_t* respBuf, size_t maxRespLen) {
  if (queryLen + 16 > maxRespLen) return 0;
  
  memcpy(respBuf, queryBuf, queryLen);
  DNSHeader* hdr = (DNSHeader*)respBuf;
  hdr->flags = htons(0x8180); // Standard response, No error
  hdr->ancount = htons(1);
  hdr->nscount = 0;
  hdr->arcount = 0;
  
  size_t offset = queryLen;
  
  // Compression pointer to domain name in Question
  respBuf[offset++] = 0xC0;
  respBuf[offset++] = 0x0C;
  
  // Type
  respBuf[offset++] = (qtype >> 8) & 0xFF;
  respBuf[offset++] = qtype & 0xFF;
  
  // Class IN
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x01;
  
  // TTL = 60s
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x3C;
  
  if (qtype == 0x001C) {
    // Type AAAA (IPv6): return :: (16 zero bytes)
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x10;
    memset(&respBuf[offset], 0x00, 16);
    offset += 16;
  } else {
    // Type A (IPv4): return 0.0.0.0
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x04;
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x00;
  }
  
  return offset;
}

void forwardDNSQuery(const uint8_t* queryBuf, size_t queryLen, const IPAddress& clientIp, uint16_t clientPort) {
  WiFiUDP upstreamUdp;
  upstreamUdp.begin(0);
  
  upstreamUdp.beginPacket(UPSTREAM_DNS_PRIMARY, 53);
  upstreamUdp.write(queryBuf, queryLen);
  if (!upstreamUdp.endPacket()) {
    upstreamUdp.beginPacket(UPSTREAM_DNS_SECONDARY, 53);
    upstreamUdp.write(queryBuf, queryLen);
    upstreamUdp.endPacket();
  }
  
  unsigned long startWait = millis();
  bool gotReply = false;
  uint8_t forwardBuf[512];
  
  while (millis() - startWait < 1500) {
    int replySize = upstreamUdp.parsePacket();
    if (replySize > 0) {
      if (replySize > sizeof(forwardBuf)) replySize = sizeof(forwardBuf);
      upstreamUdp.read(forwardBuf, replySize);
      
      dnsServer.beginPacket(clientIp, clientPort);
      dnsServer.write(forwardBuf, replySize);
      dnsServer.endPacket();
      
      gotReply = true;
      break;
    }
    delay(2);
  }
  
  upstreamUdp.stop();
}

void processDNSQueries() {
  int packetSize = dnsServer.parsePacket();
  if (packetSize <= 0) return;
  
  uint8_t buffer[512];
  if (packetSize > sizeof(buffer)) packetSize = sizeof(buffer);
  
  dnsServer.read(buffer, packetSize);
  IPAddress clientIp = dnsServer.remoteIP();
  uint16_t clientPort = dnsServer.remotePort();
  
  if (packetSize < 12) return;
  
  size_t qnameEndOffset = 0;
  uint16_t qtype = 1, qclass = 1;
  String domain = parseDNSQueryName(buffer, packetSize, qnameEndOffset, qtype, qclass);
  
  if (domain.length() == 0) return;
  
  totalQueries++;
  bool blocked = isDomainBlocked(domain);
  
  if (blocked) {
    blockedQueries++;
    uint8_t respBuf[512];
    size_t respLen = makeSinkholeResponse(buffer, packetSize, qnameEndOffset, qtype, respBuf, sizeof(respBuf));
    if (respLen > 0) {
      dnsServer.beginPacket(clientIp, clientPort);
      dnsServer.write(respBuf, respLen);
      dnsServer.endPacket();
    }
    addQueryLog(domain, clientIp, true);
  } else {
    forwardDNSQuery(buffer, packetSize, clientIp, clientPort);
    addQueryLog(domain, clientIp, false);
  }
}

// ==========================================
// PC Power Control Engine (WoL + Host Daemon)
// ==========================================
void sendWakeOnLan() {
  uint8_t magicPacket[102];
  memset(magicPacket, 0xFF, 6);
  for (int i = 1; i <= 16; i++) {
    memcpy(&magicPacket[i * 6], TARGET_PC_MAC, 6);
  }

  WiFiUDP wolUdp;
  wolUdp.begin(0);
  wolUdp.beginPacket(BROADCAST_IP, 9);
  wolUdp.write(magicPacket, sizeof(magicPacket));
  wolUdp.endPacket();

  wolUdp.beginPacket(GLOBAL_BROADCAST, 9);
  wolUdp.write(magicPacket, sizeof(magicPacket));
  wolUdp.endPacket();
  
  wolUdp.beginPacket(BROADCAST_IP, 7);
  wolUdp.write(magicPacket, sizeof(magicPacket));
  wolUdp.endPacket();
  wolUdp.stop();
  
  Serial.println("[WOL] Magic packet broadcasted to 30:9C:23:9A:B5:FA");
}

bool checkHostPcStatus() {
  HTTPClient http;
  String url = String("http://") + TARGET_PC_IP + ":" + TARGET_PC_PWR_PORT + "/api/status";
  http.begin(url);
  http.setTimeout(800);
  int httpCode = http.GET();
  bool online = (httpCode == 200);
  http.end();
  return online;
}

bool sendPowerOffSignal() {
  HTTPClient http;
  String url = String("http://") + TARGET_PC_IP + ":" + TARGET_PC_PWR_PORT + "/api/poweroff";
  http.begin(url);
  http.setTimeout(1500);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST("{\"action\":\"shutdown\",\"delay\":3}");
  bool success = (httpCode == 200 || httpCode == 202);
  http.end();
  return success;
}

// ==========================================
// Web Dashboard & REST API
// ==========================================
String buildDashboardHtml() {
  int totalDomains = flashBlocklistLoaded ? flashDomainCount : BUILTIN_BLOCKLIST_COUNT;
  float blockRate = totalQueries > 0 ? ((float)blockedQueries / totalQueries * 100.0) : 0.0;
  int apStations = WiFi.softAPgetStationNum();

  String html = F("<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
  "<title>ESP32 AdBlock & PC Control</title>"
  "<style>"
  ":root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#c9d1d9;--accent:#58a6ff;--success:#238636;--danger:#da3633;--warning:#d29922;--purple:#bc8cff}"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);padding:16px;max-width:960px;margin:0 auto;line-height:1.5}"
  ".header{display:flex;justify-content:space-between;align-items:center;padding:14px 20px;background:var(--card);border:1px solid var(--border);border-radius:12px;margin-bottom:16px}"
  ".header h1{font-size:1.3rem;display:flex;align-items:center;gap:10px;color:#fff}"
  ".badge{padding:4px 10px;border-radius:20px;font-size:0.75rem;font-weight:600;text-transform:uppercase}"
  ".badge-online{background:#1b4729;color:#3fb950;border:1px solid #238636}"
  ".badge-offline{background:#481f24;color:#f85149;border:1px solid #da3633}"
  ".badge-accent{background:#1f334d;color:var(--accent);border:1px solid var(--accent)}"
  ".badge-purple{background:#38224d;color:var(--purple);border:1px solid var(--purple)}"
  ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-bottom:16px}"
  ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px}"
  ".card-title{font-size:0.8rem;text-transform:uppercase;color:#8b949e;margin-bottom:6px}"
  ".card-value{font-size:1.6rem;font-weight:700;color:#fff}"
  ".wifi-banner{background:linear-gradient(135deg,#1f334d,#161b22);border:1px solid var(--accent);border-radius:12px;padding:16px;margin-bottom:16px}"
  ".wifi-banner h2{font-size:1.1rem;color:var(--accent);margin-bottom:8px}"
  ".btn-group{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px}"
  ".btn{padding:10px 18px;border-radius:8px;font-size:0.9rem;font-weight:600;cursor:pointer;border:none;transition:opacity 0.2s;text-decoration:none;display:inline-flex;align-items:center;justify-content:center}"
  ".btn:hover{opacity:0.85}"
  ".btn-primary{background:var(--success);color:#fff}"
  ".btn-danger{background:var(--danger);color:#fff}"
  ".btn-accent{background:var(--accent);color:#0d1117}"
  ".btn-secondary{background:#21262d;color:var(--text);border:1px solid var(--border)}"
  "table{width:100%;border-collapse:collapse;margin-top:10px;font-size:0.85rem}"
  "th,td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--border)}"
  "th{color:#8b949e;background:#10141a}"
  ".blocked{color:#f85149;font-weight:600}"
  ".allowed{color:#3fb950}"
  "input[type=text]{width:100%;padding:10px;border-radius:6px;border:1px solid var(--border);background:#0d1117;color:#fff;margin-bottom:8px}"
  "</style></head><body>");

  // Header
  html += F("<div class='header'><div><h1>🛡️ ESP32 AdBlock & Power Master</h1>"
  "<small style='color:#8b949e'>Dual AP+STA Router • Zero-Config Protection</small></div>"
  "<div><span class='badge ");
  html += hostPcOnline ? F("badge-online'>PC Online</span>") : F("badge-offline'>PC Offline</span>");
  html += F("</div></div>");

  // Wi-Fi Access Point Banner
  html += F("<div class='wifi-banner'><h2>📶 AdBlock Wi-Fi Network Active (Zero-Config)</h2>"
  "<p style='font-size:0.9rem;margin-bottom:6px'>Connect any phone, tablet, TV, or laptop directly to this Wi-Fi. <b>All ads and popups are blocked instantly with zero configuration!</b></p>"
  "<div style='display:flex;gap:16px;flex-wrap:wrap;font-size:0.95rem;margin-top:8px'>"
  "<div>SSID: <b style='color:#fff'>");
  html += AP_SSID;
  html += F("</b></div><div>Password: <b style='color:#fff'>");
  html += AP_PASSWORD;
  html += F("</b></div><div>Gateway / DNS: <b style='color:#fff'>");
  html += AP_IP.toString();
  html += F("</b></div><div>Connected Clients: <b style='color:#3fb950'>");
  html += String(apStations);
  html += F("</b></div></div></div>");

  // Statistics Grid
  html += F("<div class='grid'><div class='card'><div class='card-title'>Blocked Domains</div>"
  "<div class='card-value' style='color:var(--accent)'>");
  html += String(totalDomains);
  html += F("</div><small style='color:#8b949e'>");
  html += flashBlocklistLoaded ? F("Flash Partition Mapped (Hagezi+Video)") : F("Core Embedded");
  html += F("</small></div>"
  "<div class='card'><div class='card-title'>DNS Queries Total</div><div class='card-value'>");
  html += String(totalQueries);
  html += F("</div><small style='color:#8b949e'>Port 53 UDP</small></div>"
  "<div class='card'><div class='card-title'>Ads & Popups Blocked</div><div class='card-value' style='color:#f85149'>");
  html += String(blockedQueries);
  html += F("</div><small style='color:#8b949e'>Sinkhole 0.0.0.0 / ::</small></div>"
  "<div class='card'><div class='card-title'>Block Ratio</div><div class='card-value' style='color:#3fb950'>");
  html += String(blockRate, 1);
  html += F("%</div><small style='color:#8b949e'>Network wide</small></div></div>");

  // PC Power Controls & AdBlock Toggle
  html += F("<div class='grid' style='grid-template-columns:1fr 1fr;'><div class='card'><div class='card-title'>Target Host PC Control (192.168.0.50)</div>"
  "<p style='font-size:0.85rem;color:#8b949e;margin-bottom:8px'>Hardware WoL broadcast & secure HTTP poweroff daemon.</p>"
  "<div class='btn-group'><a href='/api/pc/wake' class='btn btn-primary'>⚡ Power On (WoL)</a>"
  "<a href='/api/pc/off' class='btn btn-danger' onclick=\"return confirm('Confirm shutdown of host PC?')\">🛑 Power Off</a></div></div>"
  "<div class='card'><div class='card-title'>AdBlock Sinkhole Engine</div>"
  "<p style='font-size:0.85rem;color:#8b949e;margin-bottom:8px'>Status: <b style='color:");
  html += adblockEnabled ? F("#3fb950'>Active</b>") : F("#f85149'>Bypassed</b>");
  html += F("</p><div class='btn-group'><a href='/api/adblock/toggle' class='btn btn-secondary'>");
  html += adblockEnabled ? F("Disable AdBlock") : F("Enable AdBlock");
  html += F("</a><a href='/api/blocklist/status' class='btn btn-secondary'>Status JSON</a></div></div></div>");

  // Domain Test Tool Card
  html += F("<div class='card' style='margin-bottom:16px'><div class='card-title'>🔍 Test Domain Lookup</div>"
  "<form action='/test' method='GET' style='margin-top:8px'>"
  "<input type='text' name='q' placeholder='e.g. doubleclick.net or ads.tiktok.com' required>"
  "<button type='submit' class='btn btn-accent'>Check Domain</button></form></div>");

  // Recent DNS Queries Log
  html += F("<div class='card'><div class='card-title'>Live DNS Queries (Last ");
  html += String(totalLogsCount);
  html += F(")</div><div style='overflow-x:auto'><table><thead><tr><th>Domain</th><th>Client IP</th><th>Action</th><th>Time</th></tr></thead><tbody>");

  int count = 0;
  int idx = (logIndex - 1 + MAX_LOGS) % MAX_LOGS;
  while (count < totalLogsCount) {
    QueryLog& entry = queryLogs[idx];
    html += F("<tr><td><code>");
    html += String(entry.domain);
    html += F("</code></td><td>");
    html += String(entry.clientIp);
    html += F("</td><td>");
    if (entry.blocked) {
      html += F("<span class='blocked'>🚫 Blocked (0.0.0.0)</span>");
    } else {
      html += F("<span class='allowed'>✓ Allowed</span>");
    }
    html += F("</td><td><small>");
    html += String(entry.timestamp);
    html += F("s</small></td></tr>");

    idx = (idx - 1 + MAX_LOGS) % MAX_LOGS;
    count++;
  }
  if (totalLogsCount == 0) {
    html += F("<tr><td colspan='4' style='text-align:center;color:#8b949e'>No queries recorded yet.</td></tr>");
  }

  html += F("</tbody></table></div></div>"
  "<p style='text-align:center;margin-top:20px;font-size:0.8rem;color:#8b949e'>ESP32 DevKit V4 • Dual Wi-Fi NAPT Router • Nexus Swarm 2026</p>"
  "</body></html>");

  return html;
}

void setupWebRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", buildDashboardHtml());
  });

  server.on("/test", HTTP_GET, []() {
    if (!server.hasArg("q")) {
      server.sendHeader("Location", "/");
      server.send(303);
      return;
    }
    String q = server.arg("q");
    bool blocked = isDomainBlocked(q);
    String res = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                 "<title>Lookup Result</title><style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:30px;max-width:600px;margin:0 auto;text-align:center}"
                 ".card{background:#161b22;padding:25px;border-radius:12px;border:1px solid #30363d;margin-top:20px}"
                 ".btn{display:inline-block;padding:10px 20px;background:#58a6ff;color:#0d1117;text-decoration:none;border-radius:6px;font-weight:bold;margin-top:15px}"
                 "</style></head><body><h2>Domain Lookup Result</h2><div class='card'>";
    res += "<p style='font-size:1.2rem'>Domain: <code>" + q + "</code></p><p style='margin:15px 0;font-size:1.5rem;font-weight:bold;color:" + (blocked ? "#f85149'>🚫 SINKHOLED (BLOCKED)" : "#3fb950'>✓ RESOLVED (ALLOWED)") + "</p>";
    res += "<a href='/' class='btn'>Back to Dashboard</a></div></body></html>";
    server.send(200, "text/html", res);
  });

  server.on("/api/status", HTTP_GET, []() {
    String json = "{";
    json += "\"host_pc_online\":" + String(hostPcOnline ? "true" : "false") + ",";
    json += "\"adblock_enabled\":" + String(adblockEnabled ? "true" : "false") + ",";
    json += "\"total_domains\":" + String(flashBlocklistLoaded ? flashDomainCount : BUILTIN_BLOCKLIST_COUNT) + ",";
    json += "\"flash_mapped\":" + String(flashBlocklistLoaded ? "true" : "false") + ",";
    json += "\"total_queries\":" + String(totalQueries) + ",";
    json += "\"blocked_queries\":" + String(blockedQueries) + ",";
    json += "\"ap_stations\":" + String(WiFi.softAPgetStationNum()) + ",";
    json += "\"ap_ip\":\"" + AP_IP.toString() + "\",";
    json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap());
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/blocklist/status", HTTP_GET, []() {
    String json = "{";
    json += "\"total_domains\":" + String(flashBlocklistLoaded ? flashDomainCount : BUILTIN_BLOCKLIST_COUNT) + ",";
    json += "\"source\":\"" + String(flashBlocklistLoaded ? "Flash Partition 0x290000 (HaGeZi+Video/Reel/Popup)" : "PROGMEM Builtin") + "\",";
    json += "\"blocked_count\":" + String(blockedQueries) + ",";
    json += "\"allowed_count\":" + String(totalQueries - blockedQueries);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/adblock/toggle", HTTP_GET, []() {
    adblockEnabled = !adblockEnabled;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/api/pc/wake", HTTP_GET, []() {
    sendWakeOnLan();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/api/pc/off", HTTP_GET, []() {
    sendPowerOffSignal();
    server.sendHeader("Location", "/");
    server.send(303);
  });
}

// ==========================================
// Setup & Main Loop
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n========================================");
  Serial.println("  ESP32 AdBlock Router & Power Master");
  Serial.println("  Dual AP+STA • Zero-Config DNS Sinkhole");
  Serial.println("========================================");

  // 1. Initialize Memory-Mapped Flash Blocklist Table
  initFlashBlocklist();

  // 2. Configure Wi-Fi Dual Mode: AP + STA
  WiFi.mode(WIFI_AP_STA);

  // Configure SoftAP
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("[WIFI AP] SoftAP started: SSID='%s', Pass='%s', IP=%s\n", AP_SSID, AP_PASSWORD, AP_IP.toString().c_str());

  // Configure Station
  IPAddress local_IP(192, 168, 0, 201);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(192, 168, 0, 1);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, IPAddress(1, 1, 1, 1));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[WIFI STA] Connecting to %s", WIFI_SSID);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 12000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI STA] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI STA] Wi-Fi connection timed out, continuing in AP mode...");
  }

  // 3. Enable lwIP NAPT Router (translates SoftAP clients to Station uplink)
  LOCK_TCPIP_CORE();
  ip_napt_enable((u32_t)WiFi.softAPIP(), 1);
  UNLOCK_TCPIP_CORE();
  Serial.printf("[NAPT] Hardware lwIP NAT Router enabled on %s!\n", WiFi.softAPIP().toString().c_str());

  // 4. Initialize DNS Server (UDP 53)
  if (dnsServer.begin(DNS_PORT)) {
    Serial.printf("[DNS] DNS Interceptor listening on UDP port %u\n", DNS_PORT);
  } else {
    Serial.println("[DNS] Failed to bind UDP port 53!");
  }

  // 5. Initialize mDNS
  if (MDNS.begin("esp32")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] Responder active at http://esp32.local");
  }

  // 6. Start Web Dashboard
  setupWebRoutes();
  server.begin();
  Serial.println("[HTTP] Web dashboard online on port 80");
  
  hostPcOnline = checkHostPcStatus();
}

void loop() {
  // 1. Service DNS queries immediately
  processDNSQueries();

  // 2. Service HTTP server
  server.handleClient();

  // 3. Periodically check Host PC status
  if (millis() - lastPcPingCheck > PC_PING_INTERVAL) {
    lastPcPingCheck = millis();
    hostPcOnline = checkHostPcStatus();
  }
}
