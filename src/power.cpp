#include "app.h"
#include <WiFi.h>
#include <SD_MMC.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// Power management
//  * deep sleep after `sleepMin` minutes without use (not while recording, playing, sending or writing a report;
//    never while on USB power). Any rocker direction wakes the device (it then boots normally).
//  * the device's own fallback network switches off after a few idle minutes when nobody is connected to it.
//  * low battery: warning toasts, and a clean shutdown (recording saved) before the cell is drained.
// The e-paper keeps the sleep screen without power, so the device shows what happened.

static volatile uint32_t lastAct = 0;
static uint32_t lowSince = 0;
static int warned = 101;

void powerActivity() { lastAct = millis(); }
uint32_t powerIdleMs() { return millis() - lastAct; }

static bool busy() {
  if (recLive().active || playLive().active) return true;
  Guard g;
  return gSend.running || gRep.running || gProg.running || gPhone.active || gUpd.checking || gUpd.installing;
}

// Save the recording, show the sleep/off screen and park the peripherals.
static void park(char why) {
  Serial.printf("[power] %s (%c) · battery %d%% %d mV · usb %d\n", why == 'O' ? "off" : "sleep", why, batteryPercent(), batteryMv(), boardVbus());
  otaConfirm();                 // a deep-sleep wake is a reset: a fresh update must not be rolled back for it
  if (recLive().active) { uint32_t id = recStop(); Serial.printf("[power] saved recording %u\n", (unsigned)id); }
  playStop();
  uiSleepScreen(why);           // full refresh, then the panel is put into hibernation
  if (WiFi.getMode() != WIFI_OFF) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
  digitalWrite(PIN_PA_EN, LOW);
  if (gSdOk) SD_MMC.end();
  boardSleepRails();            // e-paper rail off; the image stays
}

// Off with the PWR button: the PMIC cuts the power, PWR switches it back on.
[[noreturn]] void powerOff() {
  park('O');
  Serial.flush(); delay(50);
  boardPowerOff();
  delay(2000);                  // no PMIC answered: deep sleep without a wake source is the next best thing
  esp_deep_sleep_start();
}

// Deep sleep until a rocker press, or until a charger is plugged in: the device then wakes every
// USB_POLL_S seconds for a fraction of a second, sees no USB power and sleeps on (powerWakeCheck()).
static const uint32_t USB_POLL_S = 60;
[[noreturn]] static void deepSleep() {
  const uint64_t mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_PUSH) | (1ULL << PIN_BTN_DOWN);
  for (int p : {PIN_BTN_UP, PIN_BTN_PUSH, PIN_BTN_DOWN}) { rtc_gpio_init((gpio_num_t)p); rtc_gpio_set_direction((gpio_num_t)p, RTC_GPIO_MODE_INPUT_ONLY); rtc_gpio_pullup_en((gpio_num_t)p); rtc_gpio_pulldown_dis((gpio_num_t)p); }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // keeps the pull-ups alive
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)USB_POLL_S * 1000000ULL);
  Serial.flush(); delay(50);
  esp_deep_sleep_start();
}

[[noreturn]] void powerSleep(char why) {
  park(why);
  deepSleep();
}

// Called right after boardBegin(), before the display is touched: a timer wake without a charger
// goes straight back to sleep and leaves the sleep screen as it is.
void powerWakeCheck() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
  if (boardVbus()) { Serial.println("[power] charger connected, waking up"); return; }
  boardSleepRails();
  deepSleep();
}

// Called early in setup(): never start Wi-Fi on an empty cell.
void powerBootCheck() {
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  if (c == ESP_SLEEP_WAKEUP_EXT1) Serial.println("[power] woke from sleep (rocker)");
  for (int p : {PIN_BTN_UP, PIN_BTN_PUSH, PIN_BTN_DOWN}) rtc_gpio_deinit((gpio_num_t)p);   // hand the rocker back to normal GPIO
  int mv = batteryMv(), pct = batteryPercent();
  Serial.printf("[power] battery %d%% · %d mV · usb %s\n", pct, mv, boardVbus() ? "yes" : "no");
  if (!boardVbus() && mv > 2500 && mv < 3300 && pct >= 0 && pct <= 3) powerSleep('B');
  lastAct = millis();
}

void powerLoop() {
  static uint32_t lastKey = 0, last = 0; uint32_t now = millis();
  if (now - lastKey >= 200) {
    lastKey = now;
    char k = boardPwrKey();
    if (k == 'L') { if (boardVbus()) uiToast("ON CHARGER \xC2\xB7 STAYS ON"); else powerOff(); }
    if (k == 'S') { powerActivity(); uiToast("HOLD PWR TO SWITCH OFF"); }
  }
  if (now - last < 2000) return; last = now;
  bool usb = boardVbus();
  int pct = batteryPercent(), mv = batteryMv();

  // low battery: warn at 15 % and 8 %, shut down cleanly at 5 % (only when the voltage agrees, so a
  // confused fuel gauge can never put the device in a sleep loop)
  if (!usb && pct >= 0) {
    if (pct <= 15 && warned > 15) { warned = 15; uiToast("BATTERY LOW \xC2\xB7 " + String(pct) + "%"); }
    if (pct <= 8 && warned > 8) { warned = 8; uiToast("BATTERY " + String(pct) + "% \xC2\xB7 CHARGE SOON"); }
    bool empty = pct <= 5 && mv > 2500 && mv < 3450 && now > 60000;
    if (empty) { if (!lowSince) lowSince = now; if (now - lowSince > 30000) powerSleep('B'); }
    else lowSince = 0;
  } else { lowSince = 0; if (usb) warned = 101; }

  // the device's own network: allowed while in use, or while someone is connected to it
  netSetApAllowed(usb || powerIdleMs() < AP_IDLE_MS || netApClients() > 0);

  // sleep after inactivity
  // sleep after inactivity; never while on a charger
  uint16_t mins; { Guard g; mins = gSet.sleepMin; }
  if (mins && !usb && !busy() && powerIdleMs() > (uint32_t)mins * 60000UL) powerSleep('S');
}
