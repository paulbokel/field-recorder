#include "app.h"
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "web_ui.h"
#include "icons.h"

static WebServer server(80);

static void sendJson(JsonDocument& d, int code = 200) { String s; serializeJson(d, s); server.send(code, "application/json", s); }
static void ok() { server.send(200, "application/json", "{\"ok\":true}"); }
static void fail(const char* m, int code = 400) { JsonDocument d; d["error"] = m; sendJson(d, code); }
static bool body(JsonDocument& d) { return !deserializeJson(d, server.arg("plain")); }
static uint32_t pid() { return strtoul(server.pathArg(0).c_str(), nullptr, 10); }

static void jobJson(JsonObject o, const Job& j) {
  o["running"] = j.running; o["done"] = j.done; o["kind"] = String(j.kind ? j.kind : '-'); o["id"] = j.id; o["step"] = j.step; o["pct"] = j.pct; o["err"] = j.err; o["ssid"] = j.ssid;
  JsonArray d = o["det"].to<JsonArray>(); for (int i = 0; i < 4; i++) d.add(j.det[i]);
}
static void recJson(JsonObject o, const Recording& r) {
  o["id"] = r.id; o["title"] = r.title; o["ts"] = r.ts; o["dur"] = r.dur; o["status"] = r.status; o["rtype"] = r.rtype;
  JsonArray m = o["marks"].to<JsonArray>(); for (auto x : r.marks) m.add(x);
}

static void hState() {
  if (server.arg("v") == "1") powerActivity();
  RecLive rl = recLive(); PlayLive pl = playLive();
  bool online = netOnline(); String ssid = netSsid(), ip = netIp();
  std::vector<ScanNet> scan = netScan();
  JsonDocument d;
  Guard g;
  if (gPhone.active && gPhone.phase < 1) { gPhone.phase = 1; uiPoke(); }
  d["fw"] = FW_VERSION; d["url"] = deviceUrl(); d["ip"] = netIp();
  d["ota"]["slot"] = otaSlot(); d["ota"]["rolledBack"] = otaRolledBack(); d["ota"]["repo"] = UPDATE_REPO;
  d["ota"]["checking"] = gUpd.checking; d["ota"]["installing"] = gUpd.installing; d["ota"]["avail"] = gUpd.avail; d["ota"]["latest"] = gUpd.latest;
  d["ota"]["notes"] = gUpd.notes; d["ota"]["err"] = gUpd.err; d["ota"]["checked"] = gUpd.checkedAt; d["ota"]["pct"] = gUpd.pct;
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["auto"] = gSet.autoSend; dev["fullEvery"] = gSet.fullEvery; dev["batt"] = batteryPercent(); dev["usb"] = boardVbus(); dev["mv"] = batteryMv(); dev["sleepMin"] = gSet.sleepMin; dev["vol"] = gSet.volume;
  dev["used"] = (double)storageUsed(); dev["total"] = (double)storageTotal(); dev["sd"] = gSdOk; dev["partials"] = gPartials; dev["fulls"] = gFulls;
  JsonObject w = d["wifi"].to<JsonObject>();
  w["on"] = gSet.wifiOn; w["connected"] = ssid; w["ip"] = ip; w["scanning"] = netScanning();
  JsonArray nets = w["nets"].to<JsonArray>();
  for (auto& n : gSet.nets) { JsonObject o = nets.add<JsonObject>(); int sig = netSigFor(n.ssid); o["ssid"] = n.ssid; o["sec"] = n.sec; o["sig"] = sig < 0 ? 0 : sig; o["inRange"] = sig != 0; }
  JsonArray sc = w["scan"].to<JsonArray>();
  for (auto& n : scan) { JsonObject o = sc.add<JsonObject>(); o["ssid"] = n.ssid; o["sec"] = n.sec; o["sig"] = n.sig; bool saved = false; for (auto& s : gSet.nets) if (s.ssid == n.ssid) saved = true; o["saved"] = saved; }
  JsonObject h = d["hs"].to<JsonObject>();
  h["on"] = gSet.hsOn; h["up"] = netApOn(); h["name"] = gSet.hsName; h["pw"] = gSet.hsPw; h["clients"] = netApClients();
  d["client"] = server.client().remoteIP().toString();
  JsonObject wh = d["wh"].to<JsonObject>();
  wh["svc"] = gSet.whSvc; wh["keySet"] = gSet.whKey.length() > 0; wh["keyHint"] = maskKey(gSet.whKey); wh["server"] = gSet.whServer; wh["srvKeySet"] = gSet.whSrvKey.length() > 0; wh["srvKeyHint"] = maskKey(gSet.whSrvKey);
  wh["miKeySet"] = gSet.whMiKey.length() > 0; wh["miKeyHint"] = maskKey(gSet.whMiKey); wh["spk"] = gSet.whSpk;
  wh["model"] = gSet.whModel; wh["modelName"] = whModelName(); wh["modelTyped"] = whModelTyped(); wh["lang"] = gSet.whLang; wh["ok"] = gSet.whOk;
  JsonObject r = d["rec"].to<JsonObject>();
  r["active"] = rl.active; r["paused"] = rl.paused; r["t"] = rl.t; r["id"] = rl.id;
  JsonArray rm = r["marks"].to<JsonArray>(); for (auto x : rl.marks) rm.add(x);
  JsonArray lv = r["lv"].to<JsonArray>(); for (int i = 0; i < 48; i++) lv.add(rl.lv[i]);
  JsonObject p = d["play"].to<JsonObject>(); p["active"] = pl.active; p["playing"] = pl.playing; p["id"] = pl.id; p["pos"] = pl.pos; p["dur"] = pl.dur;
  jobJson(d["send"].to<JsonObject>(), gSend);
  jobJson(d["prog"].to<JsonObject>(), gProg);
  jobJson(d["rep"].to<JsonObject>(), gRep); d["rep"]["type"] = gRep.ssid; d["repSeq"] = gRepSeq;
  JsonArray rt = d["rtypes"].to<JsonArray>(); for (auto& t : reportTypes()) { JsonArray a = rt.add<JsonArray>(); a.add(t.id); a.add(t.name); }
  d["rauto"] = reportAuto(); d["aiReady"] = aiReady();
  JsonObject ph = d["phone"].to<JsonObject>(); ph["active"] = gPhone.active; ph["purpose"] = String(gPhone.purpose); ph["ssid"] = gPhone.ssid; ph["phase"] = gPhone.phase;
  JsonArray recs = d["recs"].to<JsonArray>(); for (auto& x : gRecs) recJson(recs.add<JsonObject>(), x);
  sendJson(d);
}

static void hRec() {
  Recording r; { Guard g; Recording* p = recFind(pid()); if (!p) return fail("NOT FOUND", 404); r = *p; }
  JsonDocument d; recJson(d.to<JsonObject>(), r);
  Transcript t;
  if (r.status == REC_DONE && storageLoadTranscript(r.id, t)) {
    JsonArray s = d["segs"].to<JsonArray>();
    for (size_t i = 0; i < t.segs.size(); i++) { JsonArray a = s.add<JsonArray>(); a.add(t.segs[i].first); a.add(t.segs[i].second); a.add(i < t.spk.size() ? t.spk[i] : 0); }
    if (t.spk.size()) { JsonArray n = d["names"].to<JsonArray>(); for (size_t i = 0; i < t.names.size(); i++) n.add(t.names[i]); }
    JsonArray su = d["sum"].to<JsonArray>(); for (auto& x : t.sum) su.add(x);
    JsonArray ac = d["act"].to<JsonArray>(); for (auto& x : t.act) ac.add(x);
  }
  JsonArray reps = d["reports"].to<JsonArray>();
  for (auto& x : recReports(r.id)) { JsonObject o = reps.add<JsonObject>(); o["type"] = x.type; o["name"] = x.name; o["ts"] = x.ts; o["text"] = x.text; }
  sendJson(d);
}

static String safeName(uint32_t id) {
  String n; { Guard g; Recording* r = recFind(id); n = r ? r->title : String(id); }
  String o; for (char c : n) o += (isalnum(c) || c == ' ' || c == '-') ? c : '_';
  return o;
}

// WAV streaming with Range support. Ranged replies are capped at 512 KB so a playing
// browser never blocks the (single-threaded) server for long.
static void hAudio() {
  uint32_t id = pid();
  File f = gFS ? gFS->open(recWavPath(id)) : File();
  if (!f) return fail("NOT FOUND", 404);
  size_t size = f.size(), start = 0, end = size ? size - 1 : 0; bool partial = false;
  if (server.hasHeader("Range")) {
    String r = server.header("Range");
    if (r.startsWith("bytes=")) {
      int dash = r.indexOf('-'); start = strtoul(r.substring(6, dash).c_str(), nullptr, 10);
      String e = r.substring(dash + 1); if (e.length()) end = strtoul(e.c_str(), nullptr, 10);
      if (end >= size) end = size - 1;
      if (end - start + 1 > 524288) end = start + 524287;
      partial = true;
    }
  }
  if (start >= size) { server.send(416, "text/plain", ""); return; }
  size_t len = end - start + 1;
  WiFiClient c = server.client();
  String h = String("HTTP/1.1 ") + (partial ? "206 Partial Content" : "200 OK") + "\r\nContent-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " + String(len) + "\r\n";
  if (partial) h += "Content-Range: bytes " + String(start) + "-" + String(end) + "/" + String(size) + "\r\n";
  if (server.hasArg("dl")) h += "Content-Disposition: attachment; filename=\"" + safeName(id) + ".wav\"\r\n";
  h += "Connection: close\r\n\r\n";
  c.print(h);
  f.seek(start);
  static uint8_t buf[4096];
  while (len) { size_t n = f.read(buf, min(len, sizeof buf)); if (!n || c.write(buf, n) != n) break; len -= n; }
  f.close();
  c.stop();
}

static void hText() {
  uint32_t id = pid(); Recording r; { Guard g; Recording* p = recFind(id); if (!p) return fail("NOT FOUND", 404); r = *p; }
  Transcript t; storageLoadTranscript(id, t);
  String s = r.title + "\n" + fmtStamp(r.ts) + " \xC2\xB7 " + fmtDur(r.dur) + "\n\nSUMMARY\n";
  for (auto& x : t.sum) s += "- " + x + "\n";
  s += "\nACTION ITEMS\n"; for (auto& x : t.act) s += "- " + x + "\n";
  s += "\nTRANSCRIPT\n";
  for (size_t i = 0; i < t.segs.size(); i++) { String n = speakerName(t, i); s += fmtDur(t.segs[i].first) + (n.length() ? " \xC2\xB7 " + n : String()) + "\n" + t.segs[i].second + "\n\n"; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + safeName(id) + ".txt\"");
  server.send(200, "text/plain; charset=utf-8", s);
}

static void hRename() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String t = d["title"] | ""; t.trim(); if (!t.length()) return fail("EMPTY TITLE");
  Guard g; Recording* r = recFind(pid()); if (!r) return fail("NOT FOUND", 404);
  r->title = t.substring(0, 80); storageSaveMeta(*r); uiPoke(); ok();
}
// name a speaker of a transcript: {"n": 2, "name": "Paul"} ("" = back to "Speaker 2")
static void hSpeaker() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  uint32_t id = pid(); int n = d["n"] | 0; String name = d["name"] | ""; name.trim();
  { Guard g; if (gSend.running && gSend.id == id) return fail("SENDING"); }
  Transcript t; if (!storageLoadTranscript(id, t)) return fail("NO TRANSCRIPT", 404);
  if (n < 1 || n > (int)t.names.size()) return fail("UNKNOWN SPEAKER");
  t.names[n - 1] = name.substring(0, 40);
  if (!storageSaveTranscript(id, t)) return fail("COULD NOT SAVE");
  { Guard g; gRepSeq++; }   // open transcripts (device reader, other browsers) reload
  uiPoke(); ok();
}
static void hDelete() {
  uint32_t id = pid();
  { Guard g; if (gSend.running && gSend.id == id) return fail("SENDING"); }
  RecLive rl = recLive(); if (rl.active && rl.id == id) return fail("RECORDING");
  PlayLive pl = playLive(); if (pl.active && pl.id == id) playStop();
  if (!storageDelete(id)) return fail("NOT FOUND", 404);
  uiPoke(); ok();
}
static void hSend() {
  uint32_t id = pid();
  if (!gSet.whOk) return fail("SET UP WHISPER FIRST");
  if (!sendStart(id)) return fail("ANOTHER SEND IS RUNNING");
  ok();
}

static void hPlay() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String a = d["action"] | ""; uint32_t id = d["id"] | 0;
  if (a == "start") { if (!playStart(id, d["pos"] | 0)) return fail("CANNOT PLAY"); }
  else if (a == "toggle") { PlayLive p = playLive(); if (!p.active || p.id != id) { if (!playStart(id, d["pos"] | 0)) return fail("CANNOT PLAY"); } else playToggle(); }
  else if (a == "seek") playSeekTo(d["pos"] | 0);
  else if (a == "stop") playStop();
  ok();
}

static void hRecord() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String a = d["action"] | "";
  if (a == "start") { if (!recStart()) return fail("CANNOT START"); }
  else if (a == "pause") recTogglePause();
  else if (a == "mark") recMark();
  else if (a == "stop") { uint32_t id = recStop(); if (id) onRecordingSaved(id); JsonDocument r; r["id"] = id; return sendJson(r); }
  ok();
}

static void hWifi() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String a = d["action"] | "", ssid = d["ssid"] | "", pw = d["pw"] | "", sec = d["sec"] | "WPA2";
  int i = d["i"] | -1;
  if (a == "toggle") netSetWifiOn(!gSet.wifiOn);
  else if (a == "scan") netScanStart();
  else if (a == "disconnect") netDisconnect();
  else if (a == "join") { if (!ssid.length()) return fail("NO NETWORK NAME"); if (sec != "OPEN" && pw.length() && pw.length() < 8) return fail("AT LEAST 8 CHARACTERS"); if (!joinStart(ssid, pw, pw.length() ? sec : "OPEN")) return fail("BUSY"); }
  else if (a == "connect" && i >= 0 && i < (int)gSet.nets.size()) { SavedNet n = gSet.nets[i]; if (!joinStart(n.ssid, n.pw, n.sec)) return fail("BUSY"); }
  else if (a == "forget" && i >= 0 && i < (int)gSet.nets.size()) { String s = gSet.nets[i].ssid; { Guard g; gSet.nets.erase(gSet.nets.begin() + i); } settingsSave(); if (netSsid() == s) netDisconnect(); }
  else if (a == "move" && i >= 0 && i < (int)gSet.nets.size()) { int j = i + (int)(d["dir"] | 0); if (j >= 0 && j < (int)gSet.nets.size()) { { Guard g; std::swap(gSet.nets[i], gSet.nets[j]); } settingsSave(); } }
  else return fail("UNKNOWN ACTION");
  uiPoke(); ok();
}

static void hHotspot() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  if (d["name"].is<const char*>() || d["pw"].is<const char*>()) {
    String n = d["name"] | gSet.hsName.c_str(), p = d["pw"] | gSet.hsPw.c_str(); n.trim();
    if (!n.length()) return fail("ENTER A NAME");
    if (p.length() < 8) return fail("AT LEAST 8 CHARACTERS");
    { Guard g; gSet.hsName = n.substring(0, 31); gSet.hsPw = p.substring(0, 63); }
    settingsSave(); ok(); delay(300); netRestartAp(); uiPoke(); return;
  }
  if (d["on"].is<bool>()) { bool on = d["on"]; ok(); delay(300); netSetHotspot(on); uiPoke(); return; }
  fail("NOTHING TO DO");
}

static void hWhisper() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  {
    Guard g;
    if (d["svc"].is<int>()) { gSet.whSvc = constrain(d["svc"].as<int>(), 0, 2); gSet.whOk = false; }
    if (d["key"].is<const char*>()) { gSet.whKey = d["key"].as<String>(); gSet.whKey.trim(); gSet.whOk = false; }
    if (d["modelName"].is<const char*>()) { String m = d["modelName"].as<String>(); m.trim(); whModelTyped() = m.substring(0, 80); gSet.whOk = false; }
    if (d["miKey"].is<const char*>()) { gSet.whMiKey = d["miKey"].as<String>(); gSet.whMiKey.trim(); gSet.whOk = false; }
    if (d["spk"].is<bool>()) gSet.whSpk = d["spk"];
    if (d["srvKey"].is<const char*>()) { gSet.whSrvKey = d["srvKey"].as<String>(); gSet.whSrvKey.trim(); gSet.whOk = false; }
    if (d["server"].is<const char*>()) { gSet.whServer = d["server"].as<String>(); gSet.whServer.trim(); gSet.whOk = false; }
    if (d["model"].is<int>()) { gSet.whModel = constrain(d["model"].as<int>(), 0, 2); gSet.whOk = false; }
    if (d["lang"].is<int>()) gSet.whLang = constrain(d["lang"].as<int>(), 0, 4);
  }
  settingsSave(); uiPoke(); ok();
}
static void hWhisperTest() {
  if (!whHasCred()) return fail(gSet.whSvc == 1 ? "ENTER A SERVER ADDRESS FIRST" : "ENTER AN API KEY FIRST");
  if (!whisperTestStart()) return fail("BUSY");
  uiPoke(); ok();
}

static void hDevice() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  if (d["auto"].is<bool>()) gSet.autoSend = d["auto"];
  if (d["sleepMin"].is<int>()) gSet.sleepMin = constrain(d["sleepMin"].as<int>(), 0, 240);
  if (d["fullEvery"].is<int>()) gSet.fullEvery = constrain(d["fullEvery"].as<int>(), 5, 200);
  if (d["vol"].is<int>()) { gSet.volume = constrain(d["vol"].as<int>(), 0, 100); audioSetVolume(gSet.volume); }
  settingsSave(); uiPoke(); ok();
}

// ---------- report types and reports ----------
static void hReportTypes() {
  JsonDocument d; JsonArray a = d["types"].to<JsonArray>();
  for (auto& t : reportTypes()) {
    JsonObject o = a.add<JsonObject>(); o["id"] = t.id; o["name"] = t.name; o["prompt"] = t.prompt; o["builtin"] = t.builtin; o["edited"] = t.edited;
  }
  d["auto"] = reportAuto();
  JsonObject ai = d["ai"].to<JsonObject>();
  { Guard g; ai["url"] = gSet.aiUrl; ai["keySet"] = gSet.aiKey.length() > 0; ai["keyHint"] = maskKey(gSet.aiKey); ai["model"] = gSet.aiModel; ai["whKey"] = gSet.whKey.length() > 0; }
  ai["root"] = aiRoot(); ai["modelName"] = aiModelName(); ai["ready"] = aiReady();
  sendJson(d);
}
static void hReportType() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String a = d["action"] | "save", id = d["id"] | "";
  if (a == "reset") { if (!reportReset(id)) return fail("NOT FOUND"); return ok(); }
  if (a == "delete") { if (!reportDelete(id)) return fail("NOT FOUND"); return ok(); }
  String name = d["name"] | "", prompt = d["prompt"] | ""; name.trim(); prompt.trim();
  if (!prompt.length()) return fail("WRITE A PROMPT FIRST");
  if (!id.length() && !name.length()) return fail("GIVE IT A NAME");
  JsonDocument r; r["id"] = reportSave(id, name.substring(0, 60), prompt); sendJson(r);
}
static void hReportSettings() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  if (d["auto"].is<const char*>()) reportSetAuto(d["auto"].as<String>());
  {
    Guard g;
    if (d["url"].is<const char*>()) { gSet.aiUrl = d["url"].as<String>(); gSet.aiUrl.trim(); }
    if (d["key"].is<const char*>()) { gSet.aiKey = d["key"].as<String>(); gSet.aiKey.trim(); }
    if (d["model"].is<const char*>()) { gSet.aiModel = d["model"].as<String>(); gSet.aiModel.trim(); }
  }
  settingsSave(); ok();
}
static void hRecReport() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  uint32_t id = pid(); String type = d["type"] | "";
  { Guard g; Recording* r = recFind(id); if (!r) return fail("NOT FOUND", 404); if (r->status != REC_DONE) return fail("TRANSCRIBE IT FIRST"); }
  if (!aiReady()) return fail("SET UP AN AI UNDER REPORT TYPES FIRST");
  if (!reportStart(id, type)) return fail(gRep.running ? "ANOTHER REPORT IS BEING WRITTEN" : "UNKNOWN REPORT TYPE");
  ok();
}
static void hRecType() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String t = d["type"] | ""; ReportType rt;
  if (t.length() && t != "none" && !reportType(t, rt)) return fail("UNKNOWN REPORT TYPE");
  Guard g; Recording* r = recFind(pid()); if (!r) return fail("NOT FOUND", 404);
  r->rtype = t; storageSaveMeta(*r); ok();
}
static void hRecReportDelete() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  if (!recReportDelete(pid(), d["type"] | "")) return fail("NOT FOUND");
  { Guard g; gRepSeq++; } ok();
}
static void hRecReportDl() {
  uint32_t id = pid(); String type = server.arg("type");
  for (auto& r : recReports(id)) if (r.type == type) {
    String fn = safeName(id) + " - " + r.name; String o; for (char c : fn) o += (isalnum(c) || c == ' ' || c == '-') ? c : '_';
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + o + ".md\"");
    return server.send(200, "text/markdown; charset=utf-8", r.text);
  }
  fail("NOT FOUND", 404);
}

// phone page of the QR flow posts the Wi-Fi password / API key / server here
static void hSetup() {
  JsonDocument d; if (!body(d)) return fail("BAD JSON");
  String v = d["value"] | ""; v.trim();
  Guard g;
  if (!gPhone.active) return fail("NOTHING TO SET UP");
  if (gPhone.purpose == 'w' && v.length() < 8) return fail("AT LEAST 8 CHARACTERS");
  if (!v.length()) return fail("EMPTY");
  gPhone.value = v; gPhone.phase = 2; uiPoke(); ok();
}

// firmware upload (multipart, one .bin file) into the other app slot; restarts into it when complete
static String otaErr;
static void hOtaChunk() {
  HTTPUpload& u = server.upload();
  powerActivity();
  if (u.status == UPLOAD_FILE_START) {
    otaErr = "";
    bool busy; { Guard g; busy = gSend.running || gRep.running || gProg.running; }
    if (recLive().active) otaErr = "STOP THE RECORDING FIRST";
    else if (busy) otaErr = "WAIT UNTIL SENDING IS DONE";
    else if (!boardVbus() && batteryPercent() >= 0 && batteryPercent() < 20) otaErr = "BATTERY BELOW 20% \xC2\xB7 CONNECT USB FIRST";
    else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaErr = String("CANNOT START: ") + Update.errorString();
    else { playStop(); Serial.printf("[ota] receiving %s\n", u.filename.c_str()); uiToast("UPDATING FIRMWARE"); }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaErr.length()) return;
    if (Update.write(u.buf, u.currentSize) != u.currentSize) { otaErr = String("WRITE FAILED: ") + Update.errorString(); Update.abort(); }
  } else if (u.status == UPLOAD_FILE_END) {
    if (otaErr.length()) return;
    if (!Update.end(true)) otaErr = String("NOT A VALID FIRMWARE: ") + Update.errorString();
    else Serial.printf("[ota] %u bytes written\n", (unsigned)u.totalSize);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    otaErr = "UPLOAD ABORTED";
  }
}
static void hOtaDone() {
  if (otaErr.length() || !Update.isFinished()) { String e = otaErr.length() ? otaErr : String("NO FILE RECEIVED"); otaErr = ""; return fail(e.c_str()); }
  ok();
  uiToast("FIRMWARE UPDATED \xC2\xB7 RESTARTING");
  delay(800);
  ESP.restart();
}

static void captive() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); }

static void webTask(void*) {
  for (;;) { server.handleClient(); netDnsLoop(); vTaskDelay(pdMS_TO_TICKS(2)); }
}

// every request is someone using the device, except background polling
template <typename U> static void on(const U& uri, HTTPMethod m, std::function<void()> fn) { server.on(uri, m, [fn] { powerActivity(); fn(); }); }

void webBegin() {
  const char* hk[] = {"Range"}; server.collectHeaders(hk, 1);
  on("/", HTTP_GET, [] { server.send(200, "text/html; charset=utf-8", WEB_UI); });
  for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt", "/fwlink"}) on(p, HTTP_GET, captive);
  auto png = [](const uint8_t* d, size_t n) { server.sendHeader("Cache-Control", "public, max-age=604800"); server.send_P(200, "image/png", (const char*)d, n); };
  on("/favicon.svg", HTTP_GET, [] { server.sendHeader("Cache-Control", "public, max-age=604800"); server.send(200, "image/svg+xml", ICON_SVG); });
  on("/favicon-32.png", HTTP_GET, [png] { png(ICON_32, sizeof ICON_32); });
  on("/favicon.ico", HTTP_GET, [png] { png(ICON_32, sizeof ICON_32); });
  on("/apple-touch-icon.png", HTTP_GET, [png] { png(ICON_180, sizeof ICON_180); });
  on("/apple-touch-icon-precomposed.png", HTTP_GET, [png] { png(ICON_180, sizeof ICON_180); });
  server.on("/api/state", HTTP_GET, hState);   // polling only counts as use while the page is in front (?v=1)
  on(UriBraces("/api/rec/{}"), HTTP_GET, hRec);
  on(UriBraces("/api/rec/{}/audio"), HTTP_GET, hAudio);
  on(UriBraces("/api/rec/{}/text"), HTTP_GET, hText);
  on(UriBraces("/api/rec/{}/rename"), HTTP_POST, hRename);
  on(UriBraces("/api/rec/{}/delete"), HTTP_POST, hDelete);
  on(UriBraces("/api/rec/{}/send"), HTTP_POST, hSend);
  on(UriBraces("/api/rec/{}/speaker"), HTTP_POST, hSpeaker);
  on(UriBraces("/api/rec/{}/report"), HTTP_POST, hRecReport);
  on(UriBraces("/api/rec/{}/report/delete"), HTTP_POST, hRecReportDelete);
  on(UriBraces("/api/rec/{}/rtype"), HTTP_POST, hRecType);
  on(UriBraces("/api/rec/{}/report/dl"), HTTP_GET, hRecReportDl);
  on("/api/reports", HTTP_GET, hReportTypes);
  on("/api/reports/type", HTTP_POST, hReportType);
  on("/api/reports/settings", HTTP_POST, hReportSettings);
  on("/api/play", HTTP_POST, hPlay);
  on("/api/record", HTTP_POST, hRecord);
  on("/api/wifi", HTTP_POST, hWifi);
  on("/api/hotspot", HTTP_POST, hHotspot);
  on("/api/whisper", HTTP_POST, hWhisper);
  on("/api/whisper/test", HTTP_POST, hWhisperTest);
  on("/api/device", HTTP_POST, hDevice);
  on("/api/device/clear", HTTP_POST, [] { dispRequestFull("WEB"); uiPoke(); ok(); });
  on("/api/device/restart", HTTP_POST, [] { ok(); delay(500); ESP.restart(); });
  on("/api/setup", HTTP_POST, hSetup);
  server.on("/api/ota", HTTP_POST, hOtaDone, hOtaChunk);
  on("/api/ota/check", HTTP_POST, [] { if (otaCheckStart()) return ok(); Guard g; fail(gUpd.err.length() ? gUpd.err.c_str() : "ALREADY BUSY"); });
  on("/api/ota/install", HTTP_POST, [] { if (otaInstallStart()) return ok(); Guard g; fail(gUpd.err.length() ? gUpd.err.c_str() : "ALREADY BUSY"); });
  server.onNotFound([] {
    if (netApOn() && server.hostHeader() != "192.168.4.1" && !server.uri().startsWith("/api/")) return captive();
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 10240, nullptr, 1, nullptr, 0);
}
