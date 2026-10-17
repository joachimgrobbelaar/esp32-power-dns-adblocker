#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>

// Bluetooth Classic + BLE HID Host Includes
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "esp_hid_common.h"

// ==========================================
// Network & Hardware Configuration
// ==========================================
const char* WIFI_SSID     = "NM45_2.4GHz";
const char* WIFI_PASSWORD = "M9NMjkMD";

const char* AP_SSID       = "AdBlock_WiFi";
const char* AP_PASSWORD   = "adblock123";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

// Target Host PC (Pop!_OS)
const char* TARGET_PC_IP  = "192.168.0.50";
const int   TARGET_PC_PWR_PORT = 8999;
const uint8_t TARGET_PC_MAC[6] = {0x30, 0x9C, 0x23, 0x9A, 0xB5, 0xFA};

// DNS Configuration
const unsigned int DNS_PORT = 53;
IPAddress UPSTREAM_DNS_PRIMARY(1, 1, 1, 1);
IPAddress UPSTREAM_DNS_SECONDARY(8, 8, 8, 8);
IPAddress BROADCAST_IP(192, 168, 0, 255);
IPAddress GLOBAL_BROADCAST(255, 255, 255, 255);

WebServer server(80);
WiFiUDP dnsServer;
Preferences prefs;

// ==========================================
// Runtime State & Statistics
// ==========================================
bool adblockEnabled = true;
bool hostPcOnline = false;
unsigned long lastPcPingCheck = 0;
const unsigned long PC_PING_INTERVAL = 5000;

unsigned long totalQueries = 0;
unsigned long blockedQueries = 0;

// Memory-Mapped Flash Blocklist Table
static esp_partition_mmap_handle_t mmapHandle = 0;
static const uint8_t* mmapPtr = nullptr;
static uint32_t flashDomainCount = 0;
static bool flashBlocklistLoaded = false;

// Query Log
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
// Bluetooth Dual-Mode (GameSir + Wireless Mouse) Engine
// ==========================================
static BLEUUID hidServiceUUID((uint16_t)0x1812);
static BLEUUID reportCharUUID((uint16_t)0x2A4D);

struct DiscoveredBTDevice {
  char mac[20];
  char name[48];
  int rssi;
  uint8_t transport; // 0 = BLE, 1 = BT Classic
  uint8_t addrType;
  bool isHid;
};

const int MAX_BT_DEVICES = 16;
DiscoveredBTDevice btDevices[MAX_BT_DEVICES];
int btDeviceCount = 0;

bool btInitialized = false;
bool btScanning = false;
bool btDoConnectBle = false;
bool btDoConnectClassic = false;
char btTargetMac[20] = {0};
uint8_t btTargetAddrType = 0;
String btConnectedName = "None";
String btConnectedMac = "";
bool btIsConnected = false;
uint32_t btHidPacketCount = 0;

static BLEClient* pBleClient = nullptr;

void recordDiscoveredBtDevice(const char* mac, const char* name, int rssi, uint8_t transport, uint8_t addrType, bool isHid) {
  for (int i = 0; i < btDeviceCount; i++) {
    if (strcasecmp(btDevices[i].mac, mac) == 0) {
      if (name && strlen(name) > 0 && strcmp(name, "Unknown") != 0) {
        strncpy(btDevices[i].name, name, sizeof(btDevices[i].name) - 1);
      }
      btDevices[i].rssi = rssi;
      if (isHid) btDevices[i].isHid = true;
      return;
    }
  }
  if (btDeviceCount < MAX_BT_DEVICES) {
    DiscoveredBTDevice& d = btDevices[btDeviceCount++];
    strncpy(d.mac, mac, sizeof(d.mac) - 1);
    d.mac[sizeof(d.mac) - 1] = '\0';
    strncpy(d.name, (name && strlen(name) > 0) ? name : "BT Device", sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = '\0';
    d.rssi = rssi;
    d.transport = transport;
    d.addrType = addrType;
    d.isHid = isHid;
  }
  Serial.printf("@BT_FOUND:%s:%s:%d:%s\n", mac, (name && strlen(name) > 0) ? name : "BT_Device", rssi, transport == 1 ? "CLASSIC" : "BLE");
}

static bool isTargetGamepadOrMouse(const char* name, bool isHid) {
  if (isHid) return true;
  if (!name) return false;
  String n = String(name);
  n.toLowerCase();
  return (n.indexOf("gamesir") >= 0 || n.indexOf("gamepad") >= 0 ||
          n.indexOf("controller") >= 0 || n.indexOf("mouse") >= 0 ||
          n.indexOf("xbox") >= 0 || n.indexOf("wireless") >= 0);
}

// BLE HID Input Notification Callback
static void bleHidNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (length == 0 || pData == nullptr) return;
  btHidPacketCount++;
  String usage = (length <= 5) ? "MOUSE" : "GAMEPAD";
  if (btConnectedName.indexOf("Mouse") >= 0 || btConnectedName.indexOf("mouse") >= 0) {
    usage = "MOUSE";
  } else if (btConnectedName.indexOf("GameSir") >= 0 || btConnectedName.indexOf("gamesir") >= 0) {
    usage = "GAMEPAD";
  }
  Serial.printf("@HID:%s:%s:", usage.c_str(), btConnectedMac.c_str());
  for (size_t i = 0; i < length; i++) {
    Serial.printf("%02X", pData[i]);
  }
  Serial.println();
}

// ESP-IDF Classic/Dual HIDH Callback
static void espHidhCallback(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
  esp_hidh_event_t event = (esp_hidh_event_t)id;
  esp_hidh_event_data_t* param = (esp_hidh_event_data_t*)event_data;

  switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
      if (param->open.status == ESP_OK && param->open.dev) {
        const uint8_t* bda = esp_hidh_dev_bda_get(param->open.dev);
        const char* name = esp_hidh_dev_name_get(param->open.dev);
        char macStr[20];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        btConnectedMac = String(macStr);
        btConnectedName = (name && strlen(name) > 0) ? String(name) : "GameSir/BT HID";
        btIsConnected = true;
        Serial.printf("@BT_CONNECTED:%s:%s\n", macStr, btConnectedName.c_str());
      }
      break;
    }
    case ESP_HIDH_INPUT_EVENT: {
      if (param->input.length > 0 && param->input.data) {
        btHidPacketCount++;
        esp_hid_usage_t u = param->input.usage;
        const char* usageStr = (u == ESP_HID_USAGE_MOUSE) ? "MOUSE" : "GAMEPAD";
        const uint8_t* bda = esp_hidh_dev_bda_get(param->input.dev);
        char macStr[20] = "00:00:00:00:00:00";
        if (bda) {
          snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        }
        Serial.printf("@HID:%s:%s:", usageStr, macStr);
        for (uint16_t i = 0; i < param->input.length; i++) {
          Serial.printf("%02X", param->input.data[i]);
        }
        Serial.println();
      }
      break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
      if (param->close.dev) {
        esp_hidh_dev_free(param->close.dev);
      }
      btIsConnected = false;
      break;
    }
    default:
      break;
  }
}

// Bluetooth Classic GAP Discovery Callback
static void btClassicGapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  if (event == ESP_BT_GAP_DISC_RES_EVT) {
    uint8_t* bda = param->disc_res.bda;
    char macStr[20];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[4 - 1], bda[4], bda[5]);
    char name[48] = {0};
    int rssi = -60;
    uint32_t cod = 0;

    for (int i = 0; i < param->disc_res.num_prop; i++) {
      esp_bt_gap_dev_prop_t* prop = &param->disc_res.prop[i];
      if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len > 0) {
        size_t copyLen = prop->len < (sizeof(name) - 1) ? prop->len : (sizeof(name) - 1);
        memcpy(name, prop->val, copyLen);
        name[copyLen] = '\0';
      } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR && prop->val && strlen(name) == 0) {
        uint8_t len = 0;
        uint8_t* eirName = esp_bt_gap_resolve_eir_data((uint8_t*)prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
        if (!eirName) {
          eirName = esp_bt_gap_resolve_eir_data((uint8_t*)prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
        }
        if (eirName && len > 0) {
          size_t copyLen = len < (sizeof(name) - 1) ? len : (sizeof(name) - 1);
          memcpy(name, eirName, copyLen);
          name[copyLen] = '\0';
        }
      } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
        rssi = *(int8_t*)prop->val;
      } else if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
        cod = *(uint32_t*)prop->val;
      }
    }

    bool isHid = ((cod & 0x1F00) == 0x0500); // Peripheral (Gamepad/Joystick/Mouse/Keyboard)
    recordDiscoveredBtDevice(macStr, name, rssi, 1, 0, isHid);

    // Auto-connect if GameSir or Mouse
    if (!btIsConnected && isTargetGamepadOrMouse(name, isHid)) {
      strncpy(btTargetMac, macStr, sizeof(btTargetMac) - 1);
      btConnectedName = strlen(name) > 0 ? String(name) : "BT HID Device";
      esp_bt_gap_cancel_discovery();
      btDoConnectClassic = true;
    }
  } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      btScanning = false;
    }
  }
}

class MyBleClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override {
    btIsConnected = true;
  }
  void onDisconnect(BLEClient* pclient) override {
    btIsConnected = false;
  }
};

class MyBleAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    String mac = advertisedDevice.getAddress().toString();
    String name = advertisedDevice.haveName() ? advertisedDevice.getName() : "";
    int rssi = advertisedDevice.haveRSSI() ? advertisedDevice.getRSSI() : -65;
    bool isHid = advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(hidServiceUUID);
    if (advertisedDevice.haveAppearance()) {
      uint16_t app = advertisedDevice.getAppearance();
      if (app >= 0x03C0 && app <= 0x03C5) isHid = true;
    }
    uint8_t addrType = (uint8_t)advertisedDevice.getAddressType();
    recordDiscoveredBtDevice(mac.c_str(), name.c_str(), rssi, 0, addrType, isHid);

    if (!btIsConnected && isTargetGamepadOrMouse(name.c_str(), isHid)) {
      BLEDevice::getScan()->stop();
      strncpy(btTargetMac, mac.c_str(), sizeof(btTargetMac) - 1);
      btTargetAddrType = addrType;
      btConnectedName = name.length() > 0 ? name : "BLE HID Device";
      btDoConnectBle = true;
    }
  }
};

void initBluetoothHub() {
  if (btInitialized) return;
  Serial.println("[BT] Initializing BLE HID Host for GameSir & Mouse...");
  Serial.flush();

  BLEDevice::init("ESP32-GameSir-Hub");

  BLESecurity* pSecurity = new BLESecurity();
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setAuthenticationMode(true, false, true);

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyBleAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  btInitialized = true;
  Serial.println("[BT] BLE HID Host initialized successfully!");
  Serial.flush();
}

void startBluetoothScan() {
  if (!btInitialized) initBluetoothHub();
  btScanning = true;
  Serial.println("[BT] Scanning for GameSir Controller & Wireless Mouse (BLE)...");
  Serial.flush();
  BLEDevice::getScan()->start(5, false);
}

bool parseMacString(const char* macStr, esp_bd_addr_t outBda) {
  int vals[6];
  if (sscanf(macStr, "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) == 6) {
    for (int i = 0; i < 6; i++) outBda[i] = (uint8_t)vals[i];
    return true;
  }
  return false;
}

bool connectBleHidDevice(const char* macStr, uint8_t addrType) {
  Serial.printf("[BT BLE] Connecting to %s (type %u)...\n", macStr, addrType);
  if (pBleClient == nullptr) {
    pBleClient = BLEDevice::createClient();
    pBleClient->setClientCallbacks(new MyBleClientCallback());
  }
  BLEAddress addr(macStr, (esp_ble_addr_type_t)addrType);
  if (!pBleClient->connect(addr)) {
    Serial.println("[BT BLE] Connection failed");
    return false;
  }
  pBleClient->setMTU(185);
  BLERemoteService* pService = pBleClient->getService(hidServiceUUID);
  if (pService == nullptr) {
    Serial.println("[BT BLE] Connected, checking generic characteristics");
    btConnectedMac = String(macStr);
    btIsConnected = true;
    Serial.printf("@BT_CONNECTED:%s:%s\n", macStr, btConnectedName.c_str());
    return true;
  }

  std::map<std::string, BLERemoteCharacteristic*>* pCharMap = pService->getCharacteristics();
  int subCount = 0;
  for (auto const& entry : *pCharMap) {
    BLERemoteCharacteristic* pChar = entry.second;
    if (pChar->getUUID().equals(reportCharUUID) && pChar->canNotify()) {
      pChar->registerForNotify(bleHidNotifyCallback);
      subCount++;
    }
  }
  btConnectedMac = String(macStr);
  btIsConnected = true;
  Serial.printf("@BT_CONNECTED:%s:%s (Subscribed %d reports)\n", macStr, btConnectedName.c_str(), subCount);
  return true;
}

void processBluetoothTasks() {
  if (btDoConnectBle) {
    btDoConnectBle = false;
    connectBleHidDevice(btTargetMac, btTargetAddrType);
  }
  if (btDoConnectClassic) {
    btDoConnectClassic = false;
    esp_bd_addr_t bda;
    if (parseMacString(btTargetMac, bda)) {
      Serial.printf("[BT Classic] Opening HID device %s...\n", btTargetMac);
      esp_hidh_dev_open(bda, ESP_HID_TRANSPORT_BT, 0);
    }
  }

  // Also check incoming Serial commands from Host PC Daemon
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "@CMD:BT_SCAN") {
      startBluetoothScan();
    } else if (line.startsWith("@CMD:BT_CONNECT:")) {
      // Format: @CMD:BT_CONNECT:aa:bb:cc:dd:ee:ff:type
      String rest = line.substring(16);
      int lastColon = rest.lastIndexOf(':');
      String mac = rest;
      int transport = 0;
      if (lastColon == 17) {
        mac = rest.substring(0, 17);
        transport = rest.substring(18).toInt();
      }
      strncpy(btTargetMac, mac.c_str(), sizeof(btTargetMac) - 1);
      if (transport == 1) {
        btDoConnectClassic = true;
      } else {
        btDoConnectBle = true;
      }
    }
  }
}

// ==========================================
// Built-in AdBlock Fallback Domain List
// ==========================================
const char* const BUILTIN_BLOCKLIST[] = {
  "doubleclick.net", "googleads.g.doubleclick.net", "pagead2.googlesyndication.com",
  "googlesyndication.com", "googleadservices.com", "adservice.google.com",
  "google-analytics.com", "googletagmanager.com", "admob.com", "2mdn.net",
  "imasdk.googleapis.com", "s.youtube.com", "video-ad-stats.googlevideo.com",
  "ads.tiktok.com", "analytics.tiktok.com", "business-api.tiktok.com", "log.tiktokv.com",
  "ib.tiktokv.com", "mon.tiktokv.com", "ad.oceanengine.com", "pglstatp-toutiao.com",
  "pangle.io", "pangolin-sdk-toutiao.com", "ads.unity3d.com", "auction.unityads.unity3d.com",
  "webview.unityads.unity3d.com", "a.applovin.com", "ms.applovin.com", "d.applovin.com",
  "rt.applovin.com", "net.mintegral.com", "pgl.mintegral.com", "init.supersonicads.com",
  "outcome.ironsrc.com", "ads.api.vungle.com", "config.vungle.com", "config.inmobi.com",
  "ads.inmobi.com", "live.chartboost.com", "da.chartboost.com", "ads.liftoff.io",
  "popads.net", "popcash.net", "propellerads.com", "adcash.com", "exoclick.com",
  "adblock-analytics.com", "blockadblock.com", "detectadblock.com", "getadmiral.com",
  "tinypass.com", "piano.io", "api.tinypass.com", "experience.tinypass.com",
  "criteo.com", "criteo.net", "taboola.com", "outbrain.com", "adnxs.com"
};
const size_t BUILTIN_BLOCKLIST_COUNT = sizeof(BUILTIN_BLOCKLIST) / sizeof(BUILTIN_BLOCKLIST[0]);

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

void initFlashBlocklist() {
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (!part) {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  }
  if (part) {
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, (const void**)&mmapPtr, &mmapHandle);
    if (err == ESP_OK && mmapPtr != nullptr) {
      uint32_t magic = *(const uint32_t*)mmapPtr;
      uint32_t version = *((const uint32_t*)mmapPtr + 1);
      uint32_t count = *((const uint32_t*)mmapPtr + 2);
      if (magic == 0x424C4B4C) {
        flashDomainCount = count;
        flashBlocklistLoaded = true;
        Serial.printf("[BLOCKLIST] Flash blocklist verified! Loaded %u domains (v%u)\n", count, version);
        return;
      }
    }
  }
  flashBlocklistLoaded = false;
  flashDomainCount = 0;
}

bool isDomainBlocked(const String& rawDomain) {
  if (!adblockEnabled) return false;
  String dom = rawDomain;
  dom.trim();
  dom.toLowerCase();
  if (dom == "router" || dom.endsWith(".local") || dom.endsWith(".lan") || dom == "esp32.local") {
    return false;
  }
  uint64_t fullHash = hashDomain(dom.c_str(), dom.length());
  if (binarySearchFlash(fullHash)) return true;

  const char* str = dom.c_str();
  const char* p = str;
  while ((p = strchr(p, '.')) != NULL) {
    p++;
    if (strchr(p, '.') != NULL) {
      uint64_t subHash = hashDomain(p, strlen(p));
      if (binarySearchFlash(subHash)) return true;
    }
  }
  for (size_t i = 0; i < BUILTIN_BLOCKLIST_COUNT; i++) {
    const char* bDom = BUILTIN_BLOCKLIST[i];
    if (dom == bDom || dom.endsWith(String(".") + bDom)) return true;
  }
  return false;
}

// ==========================================
// DNS Protocol Engine (UDP 53 + Local .lan Resolution)
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
  offset = 12;
  while (offset < length) {
    uint8_t len = buffer[offset++];
    if (len == 0) break;
    if ((len & 0xC0) == 0xC0) { offset++; break; }
    if (offset + len > length) return "";
    if (domain.length() > 0) domain += ".";
    for (uint8_t i = 0; i < len; i++) domain += (char)buffer[offset++];
  }
  if (offset + 4 <= length) {
    qtype = (buffer[offset] << 8) | buffer[offset + 1];
    qclass = (buffer[offset + 2] << 8) | buffer[offset + 3];
    offset += 4;
  }
  return domain;
}

size_t makeIPv4Response(const uint8_t* queryBuf, size_t queryLen, uint16_t qtype, const IPAddress& answerIp, uint8_t* respBuf, size_t maxRespLen) {
  if (queryLen + 16 > maxRespLen) return 0;
  memcpy(respBuf, queryBuf, queryLen);
  DNSHeader* hdr = (DNSHeader*)respBuf;
  hdr->flags = htons(0x8180);
  hdr->ancount = htons(1);
  hdr->nscount = 0;
  hdr->arcount = 0;
  size_t offset = queryLen;
  respBuf[offset++] = 0xC0;
  respBuf[offset++] = 0x0C;
  respBuf[offset++] = (qtype >> 8) & 0xFF;
  respBuf[offset++] = qtype & 0xFF;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x01;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x00;
  respBuf[offset++] = 0x3C;
  if (qtype == 0x001C) {
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x10;
    memset(&respBuf[offset], 0x00, 16);
    offset += 16;
  } else {
    respBuf[offset++] = 0x00;
    respBuf[offset++] = 0x04;
    respBuf[offset++] = answerIp[0];
    respBuf[offset++] = answerIp[1];
    respBuf[offset++] = answerIp[2];
    respBuf[offset++] = answerIp[3];
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
  uint8_t forwardBuf[512];
  while (millis() - startWait < 1200) {
    int replySize = upstreamUdp.parsePacket();
    if (replySize > 0) {
      if (replySize > sizeof(forwardBuf)) replySize = sizeof(forwardBuf);
      upstreamUdp.read(forwardBuf, replySize);
      dnsServer.beginPacket(clientIp, clientPort);
      dnsServer.write(forwardBuf, replySize);
      dnsServer.endPacket();
      break;
    }
    delay(1);
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
  String domLower = domain;
  domLower.toLowerCase();

  // Custom Local LAN Hostnames for instant access across all devices
  if (domLower == "pc.lan" || domLower == "remote.lan" || domLower == "tv.lan") {
    uint8_t respBuf[512];
    size_t respLen = makeIPv4Response(buffer, packetSize, qtype, IPAddress(192, 168, 0, 50), respBuf, sizeof(respBuf));
    if (respLen > 0) {
      dnsServer.beginPacket(clientIp, clientPort);
      dnsServer.write(respBuf, respLen);
      dnsServer.endPacket();
    }
    return;
  } else if (domLower == "portal.lan" || domLower == "adblock.lan" || domLower == "esp32.lan") {
    uint8_t respBuf[512];
    size_t respLen = makeIPv4Response(buffer, packetSize, qtype, IPAddress(192, 168, 0, 201), respBuf, sizeof(respBuf));
    if (respLen > 0) {
      dnsServer.beginPacket(clientIp, clientPort);
      dnsServer.write(respBuf, respLen);
      dnsServer.endPacket();
    }
    return;
  }

  bool blocked = isDomainBlocked(domain);
  if (blocked) {
    blockedQueries++;
    uint8_t respBuf[512];
    size_t respLen = makeIPv4Response(buffer, packetSize, qtype, IPAddress(0, 0, 0, 0), respBuf, sizeof(respBuf));
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
}

bool checkHostPcStatus() {
  HTTPClient http;
  String url = String("http://") + TARGET_PC_IP + ":" + TARGET_PC_PWR_PORT + "/api/status";
  http.begin(url);
  http.setTimeout(600);
  int httpCode = http.GET();
  bool online = (httpCode == 200);
  http.end();
  return online;
}

bool callHostDaemonGet(const String& pathAndQuery) {
  HTTPClient http;
  String url = String("http://") + TARGET_PC_IP + ":" + TARGET_PC_PWR_PORT + pathAndQuery;
  http.begin(url);
  http.setTimeout(1500);
  int code = http.GET();
  http.end();
  return (code == 200 || code == 202);
}

// ==========================================
// SoftAP Connected Clients & Automatic Cross-Subnet Port Mapping
// ==========================================
String getApClientsJson() {
  wifi_sta_list_t wifi_sta_list;
  wifi_sta_mac_ip_list_t adapter_sta_list;
  memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
  memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

  String json = "{\"clients\":[";
  if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
    if (esp_wifi_ap_get_sta_list_with_ip(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
      for (int i = 0; i < adapter_sta_list.num; i++) {
        if (i > 0) json += ",";
        uint8_t* m = adapter_sta_list.sta[i].mac;
        char macStr[20];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
        IPAddress cip(adapter_sta_list.sta[i].ip.addr);
        int rssi = (i < wifi_sta_list.num) ? wifi_sta_list.sta[i].rssi : -50;
        json += "{\"mac\":\"" + String(macStr) + "\",\"ip\":\"" + cip.toString() + "\",\"rssi\":" + String(rssi) + "}";

        // Auto port-map common Android/Device ports (5555 ADB, 8080 Web, 2121 FTP) so Main LAN devices can connect directly
        if (cip[0] == 192 && cip[1] == 168 && cip[2] == 4 && cip[3] >= 2) {
          uint16_t hostOffset = (cip[3] - 2) * 10;
          LOCK_TCPIP_CORE();
          ip_portmap_add(6, (u32_t)WiFi.localIP(), 15555 + hostOffset, (u32_t)cip, 5555);
          ip_portmap_add(6, (u32_t)WiFi.localIP(), 18080 + hostOffset, (u32_t)cip, 8080);
          ip_portmap_add(6, (u32_t)WiFi.localIP(), 12121 + hostOffset, (u32_t)cip, 2121);
          UNLOCK_TCPIP_CORE();
        }
      }
    }
  }
  json += "]}";
  return json;
}

#include "portal_html.h"


// ==========================================
// Web Routes Setup
// ==========================================
void setupWebRoutes() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send_P(200, "text/html", PORTAL_HTML);
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
    res += "<p style='font-size:1.2rem'>Domain: <code>" + q + "</code></p><p style='margin:15px 0;font-size:1.5rem;font-weight:bold;color:";
    res += blocked ? "#f85149'>🚫 SINKHOLED (BLOCKED)" : "#3fb950'>✓ RESOLVED (ALLOWED)";
    res += "</p><a href='/' class='btn'>Back to Portal</a></div></body></html>";
    server.send(200, "text/html", res);
  });

  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
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
    json += "\"bt_connected\":" + String(btIsConnected ? "true" : "false") + ",";
    json += "\"bt_name\":\"" + btConnectedName + "\",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"logs\":[";
    int count = 0;
    int idx = (logIndex - 1 + MAX_LOGS) % MAX_LOGS;
    while (count < totalLogsCount && count < 15) {
      if (count > 0) json += ",";
      QueryLog& entry = queryLogs[idx];
      json += "{\"domain\":\"" + String(entry.domain) + "\",\"ip\":\"" + String(entry.clientIp) + "\",\"blocked\":" + String(entry.blocked ? "true" : "false") + "}";
      idx = (idx - 1 + MAX_LOGS) % MAX_LOGS;
      count++;
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/api/ap/clients", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", getApClientsJson());
  });

  server.on("/api/bt/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"initialized\":" + String(btInitialized ? "true" : "false") + ",";
    json += "\"scanning\":" + String(btScanning ? "true" : "false") + ",";
    json += "\"connected\":" + String(btIsConnected ? "true" : "false") + ",";
    json += "\"connected_name\":\"" + btConnectedName + "\",";
    json += "\"connected_mac\":\"" + btConnectedMac + "\",";
    json += "\"packets\":" + String(btHidPacketCount) + ",";
    json += "\"devices\":[";
    for (int i = 0; i < btDeviceCount; i++) {
      if (i > 0) json += ",";
      json += "{\"mac\":\"" + String(btDevices[i].mac) + "\",\"name\":\"" + String(btDevices[i].name) + "\",\"rssi\":" + String(btDevices[i].rssi) + ",\"type\":\"" + String(btDevices[i].transport == 1 ? "CLASSIC" : "BLE") + "\"}";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/api/bt/scan", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    startBluetoothScan();
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
  });

  server.on("/api/bt/connect", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("mac")) {
      server.send(400, "application/json", "{\"error\":\"missing mac\"}");
      return;
    }
    String mac = server.arg("mac");
    int type = server.hasArg("type") ? server.arg("type").toInt() : 0;
    strncpy(btTargetMac, mac.c_str(), sizeof(btTargetMac) - 1);
    for (int i = 0; i < btDeviceCount; i++) {
      if (strcasecmp(btDevices[i].mac, mac.c_str()) == 0) {
        btConnectedName = String(btDevices[i].name);
        btTargetAddrType = btDevices[i].addrType;
        type = btDevices[i].transport;
        break;
      }
    }
    if (!btInitialized) initBluetoothHub();
    if (type == 1) btDoConnectClassic = true;
    else btDoConnectBle = true;
    server.send(200, "application/json", "{\"status\":\"connecting\",\"mac\":\"" + mac + "\"}");
  });

  server.on("/api/adblock/toggle", HTTP_GET, []() {
    adblockEnabled = !adblockEnabled;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/api/pc/wake", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    sendWakeOnLan();
    server.send(200, "application/json", "{\"status\":\"wol_sent\"}");
  });

  server.on("/api/pc/shutdown", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    callHostDaemonGet("/api/system?action=shutdown");
    server.send(200, "application/json", "{\"status\":\"shutdown_sent\"}");
  });

  server.on("/api/pc/reboot", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    callHostDaemonGet("/api/system?action=reboot");
    server.send(200, "application/json", "{\"status\":\"reboot_sent\"}");
  });

  server.on("/api/pc/sleep", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    callHostDaemonGet("/api/system?action=sleep");
    server.send(200, "application/json", "{\"status\":\"sleep_sent\"}");
  });

  server.on("/api/pc/launch", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String app = server.arg("app");
    callHostDaemonGet("/api/launch?app=" + app);
    server.send(200, "application/json", "{\"status\":\"launched\"}");
  });
}

// ==========================================
// Setup & Main Loop
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n========================================");
  Serial.println("  ESP32 Command Hub, BT HID & AdBlocker");
  Serial.println("========================================");

  // 1. Initialize Memory-Mapped Flash Blocklist Table (103,681 domains)
  initFlashBlocklist();

  // 2. Configure Wi-Fi Dual Mode: AP + STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("[WIFI AP] SoftAP started: SSID='%s', IP=%s\n", AP_SSID, AP_IP.toString().c_str());

  IPAddress local_IP(192, 168, 0, 201);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(192, 168, 0, 1);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, IPAddress(1, 1, 1, 1));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[WIFI STA] IP: %s\n", WiFi.localIP().toString().c_str());

  // 3. Enable lwIP NAPT Router + Cross-Subnet Port Forwarding
  LOCK_TCPIP_CORE();
  ip_napt_enable((u32_t)WiFi.softAPIP(), 1);
  UNLOCK_TCPIP_CORE();
  Serial.printf("[NAPT] Hardware lwIP NAT Router enabled on %s\n", WiFi.softAPIP().toString().c_str());

  // 4. Initialize DNS Server (UDP 53)
  // 4. Initialize DNS Server (UDP 53)
  dnsServer.begin(DNS_PORT);
  Serial.printf("[DNS] DNS Interceptor active on UDP %d\n", DNS_PORT);
  Serial.flush();

  // 5. Initialize Web Portal & REST APIs
  setupWebRoutes();
  server.begin();
  Serial.println("[HTTP] Command Portal online on port 80");
  Serial.flush();

  // 6. Initialize mDNS
  if (MDNS.begin("esp32")) {
    MDNS.addService("http", "tcp", 80);
  }

  // 7. Initialize Bluetooth Hub (GameSir + Wireless Mouse)
  initBluetoothHub();

  hostPcOnline = checkHostPcStatus();
}

void loop() {
  processDNSQueries();
  server.handleClient();
  processBluetoothTasks();

  if (millis() - lastPcPingCheck > PC_PING_INTERVAL) {
    lastPcPingCheck = millis();
    hostPcOnline = checkHostPcStatus();
  }
}
