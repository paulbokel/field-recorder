#include "app.h"
#include <SD_MMC.h>
#include <FFat.h>
#include <ArduinoJson.h>
#include <algorithm>

fs::FS* gFS = nullptr;
fs::FS* gCfgFS = nullptr;
bool gSdOk = false;
std::vector<Recording> gRecs;
static const char* DIR = "/rec";

String recWavPath(uint32_t id) { return String(DIR) + "/" + id + ".wav"; }
static String metaPath(uint32_t id) { return String(DIR) + "/" + id + ".json"; }
static String trPath(uint32_t id) { return String(DIR) + "/" + id + ".tr.json"; }

void wavHeader(uint8_t* h, uint32_t n) {
  auto w32 = [&](int o, uint32_t v) { h[o] = v; h[o + 1] = v >> 8; h[o + 2] = v >> 16; h[o + 3] = v >> 24; };
  auto w16 = [&](int o, uint16_t v) { h[o] = v; h[o + 1] = v >> 8; };
  memcpy(h, "RIFF", 4); w32(4, 36 + n); memcpy(h + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
  w32(24, SAMPLE_RATE); w32(28, SAMPLE_RATE * 2); w16(32, 2); w16(34, 16); memcpy(h + 36, "data", 4); w32(40, n);
}

bool storageBegin() {
#if SD_4BIT
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
#else
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
#endif
  bool ff = FFat.begin(true);   // internal flash: config always lives here (formats itself on first use)
  if (ff) gCfgFS = &FFat;
  if (SD_MMC.begin("/sdcard", !SD_4BIT)) { gFS = &SD_MMC; gSdOk = true; Serial.println("[storage] SD card mounted"); }
  else if (ff) { gFS = &FFat; Serial.println("[storage] SD failed, using internal flash"); }
  else return false;
  if (!gCfgFS) gCfgFS = gFS;
  if (!gFS->exists(DIR)) gFS->mkdir(DIR);
  return true;
}

Recording* recFind(uint32_t id) { for (auto& r : gRecs) if (r.id == id) return &r; return nullptr; }

bool storageSaveMeta(const Recording& r, bool open) {
  if (!gFS) return false;
  JsonDocument d;
  d["id"] = r.id; d["title"] = r.title; d["ts"] = r.ts; d["dur"] = r.dur; d["status"] = r.status; d["open"] = open; if (r.rtype.length()) d["rt"] = r.rtype;
  JsonArray m = d["marks"].to<JsonArray>(); for (auto x : r.marks) m.add(x);
  File f = gFS->open(metaPath(r.id), FILE_WRITE); if (!f) return false;
  serializeJson(d, f); f.close(); return true;
}

static void repair(Recording& r) {   // recording interrupted by power loss: fix WAV header from file size
  File f = gFS->open(recWavPath(r.id), "r+");
  if (!f) return;
  uint32_t n = f.size() > 44 ? f.size() - 44 : 0; n &= ~1u;
  uint8_t h[44]; wavHeader(h, n); f.seek(0); f.write(h, 44); f.close();
  r.dur = n / (SAMPLE_RATE * 2);
  storageSaveMeta(r);
}

void storageLoadIndex() {
  Guard g; gRecs.clear(); if (!gFS) return;
  File dir = gFS->open(DIR); if (!dir) return;
  std::vector<String> names;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) { String n = f.name(); f.close(); if (n.endsWith(".json") && !n.endsWith(".tr.json") && !n.endsWith(".rep.json")) names.push_back(n); }
  dir.close();
  for (auto& n : names) {
    File f = gFS->open(String(DIR) + "/" + n); if (!f) continue;
    JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close(); if (e) continue;
    Recording r; r.id = d["id"]; r.title = d["title"].as<String>(); r.ts = d["ts"]; r.dur = d["dur"]; r.status = d["status"]; r.rtype = d["rt"] | "";
    for (JsonVariant v : d["marks"].as<JsonArray>()) r.marks.push_back(v.as<uint32_t>());
    if (r.status == REC_SENDING) r.status = REC_LOCAL;
    if (!gFS->exists(recWavPath(r.id))) continue;
    if (d["open"] | false) repair(r);
    gRecs.push_back(r);
  }
  std::sort(gRecs.begin(), gRecs.end(), [](const Recording& a, const Recording& b) { return a.ts != b.ts ? a.ts > b.ts : a.id > b.id; });
  Serial.printf("[storage] %u recordings\n", (unsigned)gRecs.size());
}

bool storageDelete(uint32_t id) {
  Guard g;
  gFS->remove(recWavPath(id)); gFS->remove(metaPath(id)); gFS->remove(trPath(id)); gFS->remove(String(DIR) + "/" + id + ".rep.json");
  for (size_t i = 0; i < gRecs.size(); i++) if (gRecs[i].id == id) { gRecs.erase(gRecs.begin() + i); return true; }
  return false;
}

bool storageLoadTranscript(uint32_t id, Transcript& t) {
  t = Transcript();
  File f = gFS->open(trPath(id)); if (!f) return false;
  JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close(); if (e) return false;
  bool spk = false;
  for (JsonArray s : d["segs"].as<JsonArray>()) { t.segs.push_back({s[0].as<uint32_t>(), s[1].as<String>()}); t.spk.push_back(s[2] | 0); if (s[2] | 0) spk = true; }
  if (!spk) t.spk.clear();
  for (JsonVariant v : d["names"].as<JsonArray>()) t.names.push_back(v.as<String>());
  for (JsonVariant v : d["sum"].as<JsonArray>()) t.sum.push_back(v.as<String>());
  for (JsonVariant v : d["act"].as<JsonArray>()) t.act.push_back(v.as<String>());
  return true;
}

String speakerName(const Transcript& t, size_t seg) {
  uint8_t n = seg < t.spk.size() ? t.spk[seg] : 0;
  if (!n) return "";
  return n <= t.names.size() && t.names[n - 1].length() ? t.names[n - 1] : "Speaker " + String(n);
}

bool storageSaveTranscript(uint32_t id, const Transcript& t) {
  JsonDocument d;
  JsonArray s = d["segs"].to<JsonArray>();
  for (size_t i = 0; i < t.segs.size(); i++) { JsonArray a = s.add<JsonArray>(); a.add(t.segs[i].first); a.add(t.segs[i].second); if (i < t.spk.size() && t.spk[i]) a.add(t.spk[i]); }
  if (t.names.size()) { JsonArray n = d["names"].to<JsonArray>(); for (auto& x : t.names) n.add(x); }
  JsonArray su = d["sum"].to<JsonArray>(); for (auto& x : t.sum) su.add(x);
  JsonArray ac = d["act"].to<JsonArray>(); for (auto& x : t.act) ac.add(x);
  File f = gFS->open(trPath(id), FILE_WRITE); if (!f) return false;
  serializeJson(d, f); f.close(); return true;
}

uint64_t storageUsed() { return gSdOk ? SD_MMC.usedBytes() : gFS ? FFat.usedBytes() : 0; }   // recordings storage
uint64_t storageTotal() { return gSdOk ? SD_MMC.totalBytes() : gFS ? FFat.totalBytes() : 0; }
