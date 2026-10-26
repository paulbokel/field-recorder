#include "app.h"
#include <Wire.h>
#include <ESP_I2S.h>
#include "es8311.h"

static I2SClass i2s;
enum AMode : uint8_t { A_IDLE, A_REC, A_PLAY };
static volatile AMode amode = A_IDLE;
static bool ok = false;

// recording
static File rf; static Recording cur;
static volatile bool rPaused = false, rStop = false;
static volatile uint32_t rSamples = 0;
static uint8_t lv[48];
static std::vector<uint32_t> rMarks;
static SemaphoreHandle_t rDone;
// playback
static File pf;
static volatile bool pPlaying = false, pStop = false;
static volatile int32_t pSeek = -1;
static volatile uint32_t pId = 0, pPos = 0, pDur = 0;
static volatile int32_t pGain = 256;   // playback gain, 256 = x1

static es8311_handle_t codec = nullptr;

// ES8311 as I2S slave, MCLK 4.096 MHz (256 x 16 kHz), 16-bit, analog mic.
// Uses Waveshare's ES8311 driver from the board's 01_Audio_Test example.
static bool codecInit() {
  codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (!codec) return false;
  const es8311_clock_config_t clk = { false, false, true, SAMPLE_RATE * 256, SAMPLE_RATE };
  if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_microphone_config(codec, false);
  es8311_microphone_gain_set(codec, (es8311_mic_gain_t)MIC_DGAIN);
  es8311_voice_mute(codec, false);
  return true;
}

// 0..100 -> ES8311 DAC volume -50..0 dB (0.5 dB steps, 0xBF = 0 dB). Never above 0 dB: playback gain already normalises.
void audioSetVolume(uint8_t v) {
  if (!ok || !codec) return;
  int reg = v == 0 ? 0 : 0xBF - (100 - min<int>(v, 100));
  es8311_voice_volume_set(codec, (reg + 1) * 100 / 256 + ((reg + 1) * 100 % 256 ? 1 : 0), nullptr);
}

static void finishRec(uint8_t* wbuf, size_t& wlen) {
  if (wlen) { rf.write(wbuf, wlen); wlen = 0; }
  uint8_t h[44]; wavHeader(h, rSamples * 2); rf.seek(0); rf.write(h, 44); rf.close();
  {
    Guard g;
    cur.dur = rSamples / SAMPLE_RATE; cur.marks = rMarks;
    storageSaveMeta(cur);
    gRecs.insert(gRecs.begin(), cur);
  }
  amode = A_IDLE; rStop = false;
  xSemaphoreGive(rDone);
}

// I2S runs in stereo like Waveshare's factory firmware. The ES8311 ADC lands on one slot:
// we pick the louder one per recording. Playback duplicates the mono WAV onto both slots.
static void audioTask(void*) {
  static int16_t st[1024];          // stereo frames from/to I2S
  static int16_t mono[512];
  static uint8_t wbuf[16384];
  size_t wlen = 0; uint32_t peak = 0, sec = 0, lastFlush = 0, lastPos = 0;
  uint64_t eL = 0, eR = 0; int ch = -1; uint32_t pkL = 0, pkR = 0;
  for (;;) {
    if (amode == A_REC) {
      size_t n = i2s.readBytes((char*)st, sizeof st);
      if (rStop) { finishRec(wbuf, wlen); peak = sec = lastFlush = 0; eL = eR = 0; ch = -1; continue; }
      if (!n || rPaused) continue;
      size_t nf = n / 4;
      for (size_t i = 0; i < nf; i++) {
        int32_t l = st[2 * i], r = st[2 * i + 1];
        uint32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        if (al > pkL) pkL = al; if (ar > pkR) pkR = ar;
        if (ch < 0) { eL += al; eR += ar; }
      }
      if (ch < 0 && rSamples + nf >= SAMPLE_RATE / 4) { ch = eR > eL ? 1 : 0; Serial.printf("[rec] mic on %s slot\n", ch ? "right" : "left"); }
      int use = ch < 0 ? (eR > eL ? 1 : 0) : ch;
      for (size_t i = 0; i < nf; i++) {
        int32_t v = (int32_t)st[2 * i + use] * REC_GAIN; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; mono[i] = v;
        uint32_t a = v < 0 ? -v : v; if (a > peak) peak = a;
      }
      size_t nb = nf * 2;
      if (wlen + nb > sizeof wbuf) { rf.write(wbuf, wlen); wlen = 0; }
      memcpy(wbuf + wlen, mono, nb); wlen += nb;
      rSamples += nf; sec += nf;
      if (sec >= SAMPLE_RATE) {
        sec -= SAMPLE_RATE;
        Serial.printf("[rec] %us peak L %u R %u\n", (unsigned)(rSamples / SAMPLE_RATE), (unsigned)pkL, (unsigned)pkR); pkL = pkR = 0;
        Guard g; memmove(lv, lv + 1, 47); lv[47] = (uint8_t)min(100.0f, sqrtf(peak / 32768.0f) * 100); peak = 0;
        uiPoke();
      }
      if (rSamples - lastFlush >= SAMPLE_RATE * 30) { if (wlen) { rf.write(wbuf, wlen); wlen = 0; } rf.flush(); lastFlush = rSamples; }
    } else if (amode == A_PLAY) {
      if (pStop) { pf.close(); digitalWrite(PIN_PA_EN, LOW); pPlaying = false; pStop = false; amode = A_IDLE; Serial.println("[play] stop"); continue; }
      if (pSeek >= 0) { pPos = pSeek; pf.seek(44 + pPos * SAMPLE_RATE * 2); pSeek = -1; }
      size_t nf = 512;
      if (pPlaying) {
        size_t n = pf.read((uint8_t*)mono, sizeof mono);
        if (!n) { pPlaying = false; pPos = pDur; uiPoke(); Serial.println("[play] end"); continue; }
        nf = n / 2;
        uint32_t pk = 0;
        for (size_t i = 0; i < nf; i++) { int32_t v = (int32_t)mono[i] * pGain >> 8; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; st[2 * i] = st[2 * i + 1] = v; uint32_t a = abs(v); if (a > pk) pk = a; }
        if (pk > peak) peak = pk;
        pPos = (pf.position() - 44) / (SAMPLE_RATE * 2);
      } else memset(st, 0, nf * 4);
      size_t w = i2s.write((uint8_t*)st, nf * 4);
      if (pPos != lastPos) { Serial.printf("[play] %us peak %u wrote %u\n", (unsigned)pPos, (unsigned)peak, (unsigned)w); peak = 0; lastPos = pPos; uiPoke(); }
    } else vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool audioBegin() {
  pinMode(PIN_PA_EN, OUTPUT); digitalWrite(PIN_PA_EN, LOW);
  rDone = xSemaphoreCreateBinary();
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return false;
  i2s.setTimeout(100);
  if (!codecInit()) { Serial.println("[audio] ES8311 not responding"); return false; }
  Serial.println("[audio] ES8311 ready");
  ok = true;
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 1);
  return true;
}

bool recStart() {
  Guard g;
  if (!ok || !gFS || amode == A_REC) return false;
  if (amode == A_PLAY) { pStop = true; for (int i = 0; i < 50 && amode != A_IDLE; i++) delay(10); }
  Recording r;
  r.ts = clockValid() ? (uint32_t)time(nullptr) : 0;
  r.id = r.ts ? r.ts : 1000 + gSet.nextId++;
  while (recFind(r.id)) r.id++;
  if (r.ts) { time_t t = r.ts; struct tm tm; localtime_r(&t, &tm); char b[24]; snprintf(b, sizeof b, "Recording %02d:%02d", tm.tm_hour, tm.tm_min); r.title = b; }
  else { r.title = "Recording " + String(gRecs.size() + 1); settingsSave(); }
  rf = gFS->open(recWavPath(r.id), FILE_WRITE);
  if (!rf) return false;
  uint8_t h[44]; wavHeader(h, 0); rf.write(h, 44);
  storageSaveMeta(r, true);
  cur = r; rSamples = 0; rPaused = false; rStop = false; rMarks.clear(); memset(lv, 0, sizeof lv);
  amode = A_REC; uiPoke();
  return true;
}
void recTogglePause() { if (amode == A_REC) { rPaused = !rPaused; uiPoke(); } }
void recMark() { Guard g; if (amode == A_REC) rMarks.push_back(rSamples / SAMPLE_RATE); uiPoke(); }
uint32_t recStop() {
  if (amode != A_REC) return 0;
  xSemaphoreTake(rDone, 0);
  rStop = true;
  xSemaphoreTake(rDone, pdMS_TO_TICKS(4000));
  uiPoke();
  return cur.id;
}
RecLive recLive() {
  Guard g; RecLive l;
  l.active = amode == A_REC; l.paused = rPaused; l.t = rSamples / SAMPLE_RATE; l.marks = rMarks;
  memcpy(l.lv, lv, sizeof lv); l.id = cur.id; l.ts = cur.ts; l.title = cur.title;
  return l;
}

bool playStart(uint32_t id, uint32_t pos) {
  if (!ok || amode == A_REC) return false;
  if (amode == A_PLAY) { pStop = true; for (int i = 0; i < 50 && amode != A_IDLE; i++) delay(10); }
  File f = gFS->open(recWavPath(id)); if (!f) return false;
  uint32_t bytes = f.size() > 44 ? f.size() - 44 : 0;
  // quick peak scan (one 2 KB block every 32 KB) to lift quiet recordings to full level
  { static int16_t sb[1024]; uint32_t pk = 1;
    for (uint32_t off = 44; off < f.size(); off += 32768) { f.seek(off); size_t n = f.read((uint8_t*)sb, sizeof sb) / 2; for (size_t i = 0; i < n; i++) { uint32_t a = abs(sb[i]); if (a > pk) pk = a; } }
    pGain = constrain((int32_t)(29000L * 256 / pk), 256, 256 * 16);
    Serial.printf("[play] peak %u -> gain x%.1f\n", (unsigned)pk, pGain / 256.0f); }
  pf = f; pDur = bytes / (SAMPLE_RATE * 2); pId = id; pPos = min(pos, (uint32_t)pDur);
  pf.seek(44 + pPos * SAMPLE_RATE * 2); pSeek = -1; pStop = false; pPlaying = true;
  digitalWrite(PIN_PA_EN, HIGH);
  Serial.printf("[play] %u · %u s · PA on\n", (unsigned)id, (unsigned)pDur);
  amode = A_PLAY; uiPoke();
  return true;
}
void playToggle() {
  if (amode != A_PLAY) return;
  if (pPlaying) pPlaying = false; else { if (pPos >= pDur) { pSeek = 0; pPos = 0; } pPlaying = true; }
  uiPoke();
}
void playSeekTo(uint32_t p) { if (amode != A_PLAY) return; if (p > pDur) p = pDur; pSeek = p; pPos = p; uiPoke(); }
void playSeek(int d) { int32_t p = (int32_t)pPos + d; playSeekTo(p < 0 ? 0 : p); }
void playStop() { if (amode == A_PLAY) { pStop = true; for (int i = 0; i < 50 && amode != A_IDLE; i++) delay(10); } }
PlayLive playLive() { PlayLive l; l.active = amode == A_PLAY; l.playing = pPlaying; l.id = pId; l.pos = pPos; l.dur = pDur; return l; }
