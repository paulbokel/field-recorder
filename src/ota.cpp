#include "app.h"
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Over-the-air updates: the web interface writes a new firmware into the other app slot (Update library,
// see web.cpp) and restarts. The bootloader starts it in "pending verify" state. This file confirms it
// once it has run fine for a while; if it crashes or hangs before that, the bootloader goes back to the
// previous firmware on the next start.

static const uint32_t CONFIRM_MS = 30000;    // ran this long with the UI loop alive: keep it
static const uint32_t GIVE_UP_MS = 120000;   // still not confirmed (stuck somewhere): roll back
static bool pending = false;
static uint32_t lastCheckMs = 0;   // millis() of the last GitHub check, 0 = none yet

// Arduino's start-up code would otherwise confirm every image before setup() even runs
bool verifyRollbackLater() { return true; }

static void giveUp(void*) {
  if (!pending) return;
  Serial.println("[ota] new firmware never confirmed, rolling back");
  esp_ota_mark_app_invalid_rollback_and_reboot();
}

void otaBegin() {
  esp_ota_img_states_t st;
  pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Serial.printf("[ota] running %s%s%s\n", otaSlot().c_str(), pending ? " · new, waiting to confirm" : "", otaRolledBack() ? " · last update was rolled back" : "");
  if (!pending) return;
  static esp_timer_handle_t t;
  esp_timer_create_args_t a = {}; a.callback = giveUp; a.name = "ota";
  if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, (uint64_t)GIVE_UP_MS * 1000);
}

void otaConfirm() {
  if (!pending) return;
  pending = false;
  esp_ota_mark_app_valid_cancel_rollback();
  Serial.println("[ota] new firmware confirmed");
}

void otaLoop() {
  if (pending && millis() > CONFIRM_MS) otaConfirm();
  // first check about 20 s after going online, then every UPDATE_EVERY_H hours
  static uint32_t onlineSince = 0;
  if (!netOnline()) { onlineSince = 0; return; }
  if (!onlineSince) onlineSince = millis();
  bool due = lastCheckMs ? millis() - lastCheckMs > UPDATE_EVERY_H * 3600000UL : millis() - onlineSince > 20000;
  if (due) otaCheckStart();
}

String otaSlot() { const esp_partition_t* p = esp_ota_get_running_partition(); return p ? p->label : "?"; }

bool otaRolledBack() {
  const esp_partition_t* p = esp_ota_get_last_invalid_partition();
  esp_ota_img_states_t st;
  return p && esp_ota_get_state_partition(p, &st) == ESP_OK && (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED);
}

// ---- updates from GitHub ------------------------------------------------------------------------
// https://github.com/<repo>/releases/latest/download/manifest.json  ->  {"version":"1.2.0","size":1733136,"notes":"..."}
// firmware: https://github.com/<repo>/releases/download/v<version>/firmware.bin
// TLS is checked against the built-in root certificate bundle.

UpdState gUpd;

static bool newer(const String& a, const String& b) {   // a > b, both "x.y.z"
  int x[3] = {0}, y[3] = {0};
  sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]); sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
  return false;
}

static void checkTask(void*) {
  String err, ver, notes; uint32_t size = 0;
  {
    NetworkClientSecure c; c.useBuiltinCACertBundle();
    HTTPClient h; h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); h.setTimeout(15000);
    if (!h.begin(c, "https://github.com/" UPDATE_REPO "/releases/latest/download/manifest.json")) err = "NO CONNECTION";
    else {
      int code = h.GET();
      if (code == 404) err = "NO RELEASE YET";
      else if (code != 200) err = "GITHUB ANSWERED " + String(code);
      else {
        JsonDocument d;
        if (deserializeJson(d, h.getString())) err = "BAD MANIFEST";
        else { ver = d["version"] | ""; notes = d["notes"] | ""; size = d["size"] | 0; ver.replace("v", ""); if (!ver.length()) err = "BAD MANIFEST"; }
      }
      h.end();
    }
  }
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  if (!err.length() && size && slot && size > slot->size) err = "UPDATE TOO BIG FOR THIS LAYOUT";
  bool avail = !err.length() && newer(ver, FW_VERSION);
  { Guard g; gUpd.checking = false; gUpd.err = err; if (!err.length()) { gUpd.latest = ver; gUpd.notes = notes; gUpd.avail = avail; gUpd.checkedAt = time(nullptr); } }
  Serial.printf("[ota] check: %s\n", err.length() ? err.c_str() : avail ? ("update " + ver).c_str() : "up to date");
  if (avail) uiToast("UPDATE " + ver + " AVAILABLE");
  uiPoke();
  vTaskDelete(nullptr);
}

bool otaCheckStart() {
  if (!netOnline()) { Guard g; gUpd.err = "NO INTERNET"; return false; }
  { Guard g; if (gUpd.checking || gUpd.installing) return false; gUpd.checking = true; gUpd.err = ""; }
  lastCheckMs = millis(); if (!lastCheckMs) lastCheckMs = 1;
  uiPoke();
  return xTaskCreatePinnedToCore(checkTask, "updcheck", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}

static void installTask(void*) {
  String ver; { Guard g; ver = gUpd.latest; }
  String url = "https://github.com/" UPDATE_REPO "/releases/download/v" + ver + "/firmware.bin";
  Serial.printf("[ota] installing %s\n", url.c_str());
  NetworkClientSecure c; c.useBuiltinCACertBundle();
  HTTPUpdate u(30000);
  u.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  u.rebootOnUpdate(false);
  u.onProgress([](int cur, int total) { static int last = -1; int p = total > 0 ? (int)((int64_t)cur * 100 / total) : 0; if (p != last) { last = p; { Guard g; gUpd.pct = p; } powerActivity(); if (p % 5 == 0) uiPoke(); } });
  t_httpUpdate_return r = u.update(c, url);
  if (r == HTTP_UPDATE_OK) {
    Serial.println("[ota] installed, restarting");
    { Guard g; gUpd.pct = 100; }
    uiToast("UPDATED \xC2\xB7 RESTARTING"); uiPoke();
    delay(1500);
    ESP.restart();
  }
  String e = r == HTTP_UPDATE_NO_UPDATES ? String("NOTHING TO INSTALL") : "FAILED: " + u.getLastErrorString();
  e.toUpperCase();
  Serial.printf("[ota] %s\n", e.c_str());
  { Guard g; gUpd.installing = false; gUpd.err = e; }
  uiToast("UPDATE FAILED"); uiPoke();
  vTaskDelete(nullptr);
}

bool otaInstallStart() {
  Guard g;
  if (gUpd.installing || gUpd.checking) return false;
  if (!gUpd.avail) { gUpd.err = "NO UPDATE AVAILABLE"; return false; }
  if (!netOnline()) { gUpd.err = "NO INTERNET"; return false; }
  if (recLive().active) { gUpd.err = "STOP THE RECORDING FIRST"; return false; }
  if (gSend.running || gRep.running || gProg.running) { gUpd.err = "WAIT UNTIL SENDING IS DONE"; return false; }
  if (!boardVbus() && batteryPercent() >= 0 && batteryPercent() < 20) { gUpd.err = "BATTERY BELOW 20% \xC2\xB7 CONNECT USB"; return false; }
  playStop();
  gUpd.installing = true; gUpd.pct = 0; gUpd.err = "";
  uiPoke();
  if (xTaskCreatePinnedToCore(installTask, "updinst", 10240, nullptr, 1, nullptr, 0) != pdPASS) { gUpd.installing = false; gUpd.err = "NO MEMORY"; return false; }
  return true;
}
