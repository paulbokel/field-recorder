#include "app.h"
#include <Wire.h>
#include <sys/time.h>

static SemaphoreHandle_t i2cMx;
static bool pmic = false;
static int battCache = -1; static uint32_t battAt = 0;

static bool wr(uint8_t a, uint8_t r, uint8_t v) { Wire.beginTransmission(a); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
static int rd(uint8_t a, uint8_t r) {
  Wire.beginTransmission(a); Wire.write(r);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)a, 1) != 1) return -1;
  return Wire.read();
}

void boardBegin() {
  i2cMx = xSemaphoreCreateMutex();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  pmic = rd(ADDR_PMIC, 0x03) >= 0;
  if (pmic) {
    // TG28 = AXP2101-compatible. Same rails as Waveshare's factory firmware:
    // DC1 3.3 V, ALDO1-3 3.3 V (ALDO3 = e-paper). Set voltages first, then enable.
    wr(ADDR_PMIC, 0x82, (3300 - 1500) / 100);
    for (uint8_t r = 0x92; r <= 0x94; r++) wr(ADDR_PMIC, r, (3300 - 500) / 100);
    int en = rd(ADDR_PMIC, 0x90);
    if (en >= 0) wr(ADDR_PMIC, 0x90, en | 0x07);
    int adc = rd(ADDR_PMIC, 0x30);             // battery voltage ADC (fuel gauge input)
    if (adc >= 0) wr(ADDR_PMIC, 0x30, adc | 0x01);
    // charger, same values as Waveshare's factory firmware (XPowersLib names in comments)
    auto upd = [](uint8_t r, uint8_t keep, uint8_t v) { int x = rd(ADDR_PMIC, r); if (x >= 0) wr(ADDR_PMIC, r, (x & keep) | v); };
    upd(0x15, 0xF0, 6);   // VBUS voltage limit 4.36 V
    upd(0x16, 0xF8, 4);   // VBUS current limit 1500 mA
    upd(0x24, 0xF8, 0);   // system power-down voltage 2.6 V
    upd(0x61, 0xFC, 2);   // pre-charge 50 mA (wakes a deeply discharged cell)
    upd(0x62, 0xE0, 8);   // constant charge current 200 mA
    upd(0x63, 0xF0, 1);   // termination 25 mA
    upd(0x64, 0xF8, 3);   // charge to 4.2 V
    upd(0x68, 0xFE, 1);   // battery detection on
    int g = rd(ADDR_PMIC, 0x18);               // fuel gauge enable
    if (g >= 0) wr(ADDR_PMIC, 0x18, g | 0x08 | 0x02);   // fuel gauge + cell charging on
    // PWR button: "long press" after 1.5 s (we draw the off screen and switch off ourselves),
    // hard power-off by the PMIC only after 10 s so it never cuts the screen update short
    upd(0x27, 0xC3, (1 << 4) | (3 << 2));
    upd(0x41, 0xF3, 0x0C);                    // IRQ enable: PWR short + long press
    wr(ADDR_PMIC, 0x49, 0x0C);                // clear a press left over from switching on
    delay(30);
  }
  Serial.printf("[board] PMIC %s\n", pmic ? "found" : "not found");
}

int batteryPercent() {
  if (!pmic) return -1;
  if (battAt && millis() - battAt < 30000) return battCache;
  xSemaphoreTake(i2cMx, portMAX_DELAY);
  int st = rd(ADDR_PMIC, 0x00), p = rd(ADDR_PMIC, 0xA4);
  xSemaphoreGive(i2cMx);
  battCache = (st >= 0 && !(st & 0x08)) || p < 0 || p > 100 ? -1 : p;
  battAt = millis();
  return battCache;
}

bool boardVbus() {
  if (!pmic) return false;
  xSemaphoreTake(i2cMx, portMAX_DELAY); int st = rd(ADDR_PMIC, 0x00); xSemaphoreGive(i2cMx);
  return st >= 0 && (st & 0x20);
}
int batteryMv() {
  if (!pmic) return 0;
  xSemaphoreTake(i2cMx, portMAX_DELAY); int h = rd(ADDR_PMIC, 0x34), l = rd(ADDR_PMIC, 0x35), st = rd(ADDR_PMIC, 0x00); xSemaphoreGive(i2cMx);
  if (h < 0 || l < 0 || st < 0 || !(st & 0x08)) return 0;
  return ((h & 0x3F) << 8) | l;
}
void boardSleepRails() {
  if (!pmic) return;
  xSemaphoreTake(i2cMx, portMAX_DELAY); int en = rd(ADDR_PMIC, 0x90); if (en >= 0) wr(ADDR_PMIC, 0x90, en & ~0x04); xSemaphoreGive(i2cMx);   // ALDO3 = e-paper
}

char boardPwrKey() {
  if (!pmic) return 0;
  xSemaphoreTake(i2cMx, portMAX_DELAY);
  int st = rd(ADDR_PMIC, 0x49);
  if (st > 0 && (st & 0x0C)) wr(ADDR_PMIC, 0x49, st & 0x0C);   // write 1 to clear
  xSemaphoreGive(i2cMx);
  if (st < 0) return 0;
  return st & 0x04 ? 'L' : st & 0x08 ? 'S' : 0;
}

void boardPowerOff() {
  if (!pmic) return;
  xSemaphoreTake(i2cMx, portMAX_DELAY); int c = rd(ADDR_PMIC, 0x10); if (c >= 0) wr(ADDR_PMIC, 0x10, c | 0x01); xSemaphoreGive(i2cMx);
}

static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t tobcd(int v) { return ((v / 10) << 4) | (v % 10); }
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

// RTC keeps UTC
void rtcToSystem() {
  xSemaphoreTake(i2cMx, portMAX_DELAY);
  Wire.beginTransmission(ADDR_RTC); Wire.write(0x04);
  bool ok = Wire.endTransmission(false) == 0 && Wire.requestFrom((int)ADDR_RTC, 7) == 7;
  uint8_t b[7] = {0}; if (ok) for (int i = 0; i < 7; i++) b[i] = Wire.read();
  xSemaphoreGive(i2cMx);
  if (!ok || (b[0] & 0x80)) { Serial.println("[rtc] not set"); return; }
  int y = 2000 + bcd2(b[6]), mo = bcd2(b[5] & 0x1F), d = bcd2(b[3] & 0x3F);
  int64_t t = daysFromCivil(y, mo, d) * 86400 + bcd2(b[2] & 0x3F) * 3600 + bcd2(b[1] & 0x7F) * 60 + bcd2(b[0] & 0x7F);
  if (y < 2024) return;
  struct timeval tv = {(time_t)t, 0}; settimeofday(&tv, nullptr);
  Serial.println("[rtc] clock restored");
}

void systemToRtc() {
  time_t now = time(nullptr); struct tm u; gmtime_r(&now, &u);
  xSemaphoreTake(i2cMx, portMAX_DELAY);
  Wire.beginTransmission(ADDR_RTC); Wire.write(0x04);
  Wire.write(tobcd(u.tm_sec)); Wire.write(tobcd(u.tm_min)); Wire.write(tobcd(u.tm_hour)); Wire.write(tobcd(u.tm_mday));
  Wire.write(u.tm_wday); Wire.write(tobcd(u.tm_mon + 1)); Wire.write(tobcd(u.tm_year - 100));
  Wire.endTransmission();
  xSemaphoreGive(i2cMx);
}
