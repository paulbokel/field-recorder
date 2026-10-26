#include "app.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <esp_sntp.h>
#include <algorithm>

static DNSServer dns;
static bool apUp = false, apTemp = false, apAuto = false, apAllowed = true, mdnsUp = false, ntpStarted = false, rtcSaved = false;
static std::vector<ScanNet> scanRes; static bool scanning = false, haveScan = false; static uint32_t scanAt = 0;
static SemaphoreHandle_t joinMx;
static volatile bool assoc = false, gotIp = false; static volatile int reason = 0;
static uint32_t lastAuto = 0, suppressUntil = 0, offlineSince = 0;

static String secName(wifi_auth_mode_t a) {
  if (a == WIFI_AUTH_OPEN) return "OPEN";
  if (a == WIFI_AUTH_WPA3_PSK || a == WIFI_AUTH_WPA2_WPA3_PSK) return "WPA3";
  if (a == WIFI_AUTH_WEP) return "WEP";
  return "WPA2";
}
static uint8_t sigOf(int r) { return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1; }

static void onEvt(arduino_event_id_t e, arduino_event_info_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_CONNECTED) assoc = true;
  else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true;
  else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { assoc = gotIp = false; reason = info.wifi_sta_disconnected.reason; }
  else return;
  uiPoke();
}

static void applyMode() {
  bool ap = gSet.hsOn || apTemp || apAuto, sta = gSet.wifiOn;
  if (!ap && apUp) { dns.stop(); WiFi.softAPdisconnect(true); apUp = false; }
  WiFi.mode(ap ? (sta ? WIFI_AP_STA : WIFI_AP) : (sta ? WIFI_STA : WIFI_OFF));
  if (ap && !apUp) {
    WiFi.softAP(gSet.hsName.c_str(), gSet.hsPw.c_str());
    delay(100);
    dns.start(53, "*", WiFi.softAPIP());
    apUp = true;
  }
  if ((ap || sta) && !mdnsUp && MDNS.begin(deviceName().c_str())) { MDNS.addService("http", "tcp", 80); MDNS.addServiceTxt("http", "tcp", "board", "field-recorder"); mdnsUp = true; Serial.printf("[net] web interface at %s\n", deviceUrl().c_str()); }
  uiPoke();
}

static bool badReason(int r) {
  return r == WIFI_REASON_NO_AP_FOUND || r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_CONNECTION_FAIL;
}

static void autoConnect() {
  if (xSemaphoreTake(joinMx, 0) != pdTRUE) return;
  std::vector<SavedNet> nets; { Guard g; nets = gSet.nets; }
  for (auto& n : nets) {
    if (!gSet.wifiOn || WiFi.status() == WL_CONNECTED) break;
    if (netSigFor(n.ssid) == 0) continue;
    reason = 0; assoc = gotIp = false;
    WiFi.begin(n.ssid.c_str(), n.pw.length() ? n.pw.c_str() : nullptr);
    uint32_t t0 = millis();
    while (millis() - t0 < 12000 && !gotIp) { if (millis() - t0 > 1500 && badReason(reason)) break; vTaskDelay(pdMS_TO_TICKS(100)); }
    if (gotIp) { Serial.printf("[net] joined %s\n", n.ssid.c_str()); break; }
  }
  xSemaphoreGive(joinMx);
}

static void netTask(void*) {
  for (;;) {
    if (scanning) {
      int n = WiFi.scanComplete();
      if (n >= 0) {
        std::vector<ScanNet> r;
        for (int i = 0; i < n; i++) {
          String s = WiFi.SSID(i); if (!s.length()) continue;
          int rs = WiFi.RSSI(i);
          auto it = std::find_if(r.begin(), r.end(), [&](const ScanNet& x) { return x.ssid == s; });
          if (it != r.end()) { if (rs > it->rssi) { it->rssi = rs; it->sig = sigOf(rs); } continue; }
          ScanNet sn; sn.ssid = s; sn.sec = secName(WiFi.encryptionType(i)); sn.rssi = rs; sn.sig = sigOf(rs); r.push_back(sn);
        }
        std::sort(r.begin(), r.end(), [](const ScanNet& a, const ScanNet& b) { return a.rssi > b.rssi; });
        WiFi.scanDelete();
        { Guard g; scanRes = r; haveScan = true; scanAt = millis(); scanning = false; }
        uiPoke();
      } else if (n == WIFI_SCAN_FAILED) scanning = false;
    }
    // Web interface must always be reachable: when not on Wi-Fi, keep the device's own network up.
    bool online = netOnline();
    if (online) { offlineSince = 0; if (apAuto) { apAuto = false; Serial.println("[net] online, fallback AP off"); applyMode(); } }
    else {
      if (!offlineSince) offlineSince = millis();
      bool joining = xSemaphoreTake(joinMx, 0) != pdTRUE; if (!joining) xSemaphoreGive(joinMx);
      if (apAuto && !apAllowed) { apAuto = false; Serial.println("[net] idle, fallback AP off"); applyMode(); }
      if (!apAuto && apAllowed && !joining && millis() - offlineSince > (gSet.wifiOn && gSet.nets.size() ? 25000u : 1000u)) { apAuto = true; Serial.println("[net] offline, fallback AP on"); applyMode(); }
    }
    // retry saved networks; back off while someone is using the device's own network (rejoining hops channels)
    uint32_t every = netApClients() > 0 ? 180000 : powerIdleMs() > AP_IDLE_MS ? 300000 : 20000;
    if (gSet.wifiOn && !scanning && WiFi.status() != WL_CONNECTED && (int32_t)(millis() - suppressUntil) > 0 &&
        (!lastAuto || millis() - lastAuto > every)) { lastAuto = millis(); autoConnect(); }
    if (netOnline() && !ntpStarted) { configTzTime(TZ_INFO, NTP_SERVER); ntpStarted = true; }
    if (ntpStarted && !rtcSaved && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) { systemToRtc(); rtcSaved = true; uiPoke(); }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void netBegin() {
  joinMx = xSemaphoreCreateMutex();
  Network.begin();   // start the TCP/IP stack even with Wi-Fi and hotspot off, else webBegin() asserts in lwIP
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onEvt);
  WiFi.setHostname(deviceName().c_str());   // routers also list it under this name
  applyMode();
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 2, nullptr, 0);
}

void netDnsLoop() { if (apUp) dns.processNextRequest(); }
bool netOnline() { return gSet.wifiOn && WiFi.status() == WL_CONNECTED; }
String netSsid() { return netOnline() ? WiFi.SSID() : String(); }
String netIp() { return netOnline() ? WiFi.localIP().toString() : String(); }
bool netApOn() { return apUp; }
bool netApAuto() { return apAuto && !gSet.hsOn; }
String netWebUrl() {
  if (netOnline()) return "http://" + WiFi.localIP().toString();
  if (apUp) return "http://" + WiFi.softAPIP().toString();
  return "";
}
int netApClients() { return apUp ? WiFi.softAPgetStationNum() : 0; }
void netSetWifiOn(bool on) { { Guard g; gSet.wifiOn = on; } settingsSave(); if (!on) WiFi.disconnect(); applyMode(); lastAuto = 0; suppressUntil = 0; }
void netSetHotspot(bool on) { { Guard g; gSet.hsOn = on; } settingsSave(); applyMode(); }
void netSetApAllowed(bool on) { apAllowed = on; }
void netSetApTemp(bool on) { apTemp = on; applyMode(); }
void netRestartAp() { if (apUp) { dns.stop(); WiFi.softAPdisconnect(true); apUp = false; } applyMode(); }
void netDisconnect() { suppressUntil = millis() + 600000; WiFi.disconnect(); uiPoke(); }
void netReconnectSoon() { suppressUntil = 0; lastAuto = 0; }

void netScanStart() {
  if (!gSet.wifiOn || scanning) return;
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true, false, false, 300) == WIFI_SCAN_FAILED) return;
  scanning = true; uiPoke();
}
bool netScanning() { return scanning; }
bool netHaveScan() { return haveScan; }
std::vector<ScanNet> netScan() { Guard g; return scanRes; }
int netSigFor(const String& ssid) {
  Guard g;
  if (netOnline() && WiFi.SSID() == ssid) return sigOf(WiFi.RSSI());
  if (!haveScan || millis() - scanAt > 300000) return -1;
  for (auto& s : scanRes) if (s.ssid == ssid) return s.sig;
  return 0;
}

bool netCheckInternet() {
  WiFiClient c; HTTPClient h; h.setConnectTimeout(6000); h.setTimeout(6000);
  if (!h.begin(c, "http://connectivitycheck.gstatic.com/generate_204")) return false;
  int code = h.GET(); h.end();
  return code == 204 || code == 200;
}

bool netJoin(const String& ssid, const String& pw, std::function<void(int, const String&)> step, String& err) {
  xSemaphoreTake(joinMx, portMAX_DELAY);
  if (!gSet.wifiOn) { { Guard g; gSet.wifiOn = true; } settingsSave(); applyMode(); }
  suppressUntil = 0;
  WiFi.disconnect(); vTaskDelay(pdMS_TO_TICKS(300));
  reason = 0; assoc = gotIp = false;
  WiFi.begin(ssid.c_str(), pw.length() ? pw.c_str() : nullptr);
  uint32_t t0 = millis(); bool joined = false;
  while (millis() - t0 < 15000) { if (assoc || gotIp) { joined = true; break; } if (millis() - t0 > 1500 && badReason(reason)) break; vTaskDelay(pdMS_TO_TICKS(100)); }
  if (!joined) {
    int r = reason;
    err = r == WIFI_REASON_NO_AP_FOUND ? "NETWORK NOT FOUND" : (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT) ? "WRONG PASSWORD" : "COULD NOT JOIN";
    WiFi.disconnect(); xSemaphoreGive(joinMx); return false;
  }
  step(1, "");
  t0 = millis(); while (millis() - t0 < 12000 && !gotIp) vTaskDelay(pdMS_TO_TICKS(100));
  if (!gotIp) { err = "NO IP ADDRESS"; xSemaphoreGive(joinMx); return false; }
  step(2, WiFi.localIP().toString());
  xSemaphoreGive(joinMx);
  if (!netCheckInternet()) { err = "CONNECTED · NO INTERNET"; return false; }
  return true;
}
