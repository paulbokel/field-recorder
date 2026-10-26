#include "app.h"
#include <time.h>
#include <esp_sleep.h>

SemaphoreHandle_t gLock;
PhoneSetup gPhone;

static const char* MON[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};

String fmtDur(uint32_t s) {
  char b[16]; unsigned h = s / 3600, m = s % 3600 / 60, x = s % 60;
  if (h) snprintf(b, sizeof b, "%u:%02u:%02u", h, m, x); else snprintf(b, sizeof b, "%02u:%02u", m, x);
  return b;
}
String fmtStamp(uint32_t ts) {
  if (!ts) return "NO DATE";
  time_t t = ts; struct tm tm; localtime_r(&t, &tm);
  char b[32]; snprintf(b, sizeof b, "%d %s \xC2\xB7 %02d:%02d", tm.tm_mday, MON[tm.tm_mon], tm.tm_hour, tm.tm_min);
  return b;
}
String upper(String s) { s.toUpperCase(); return s; }
bool clockValid() { return time(nullptr) > 1700000000; }

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {   // a charger check from deep sleep must be quick
    for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2500;) delay(10);   // let USB CDC attach so the boot log is visible
    delay(300);
  }
  Serial.printf("\n[boot] Field Recorder %s\n", FW_VERSION);
  gLock = xSemaphoreCreateRecursiveMutex();
  boardBegin();
  powerWakeCheck();
  settingsLoad();
  setenv("TZ", TZ_INFO, 1); tzset();
  rtcToSystem();
  dispBegin();
  powerBootCheck();
  if (!storageBegin()) Serial.println("[storage] no SD card and no internal FAT");
  storageLoadIndex();
  reportsBegin();
  if (!audioBegin()) Serial.println("[audio] I2S/codec init failed");
  audioSetVolume(gSet.volume);
  uiPinGate();
  netBegin();
  webBegin();
  uiBegin();
  otaBegin();
}

void loop() { uiLoop(); otaLoop(); }
