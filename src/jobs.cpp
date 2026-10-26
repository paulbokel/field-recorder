#include "app.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

Job gSend, gProg, gRep;
uint32_t gRepSeq = 0;

struct WhCfg { uint8_t svc, model, lang; String key, server, modelName; };
static WhCfg snap() { Guard g; return {gSet.whSvc, gSet.whModel, gSet.whLang, gSet.whSvc == 0 ? gSet.whKey : gSet.whSrvKey, gSet.whServer, whModelName()}; }

static const char* SUMMARY_PROMPT =
  "You summarise meeting and voice-memo transcripts. Reply with JSON only: "
  "{\"summary\":[3-6 short factual sentences],\"actions\":[\"Owner: task\", ...]}. "
  "Use the transcript's language. Use an empty actions array when there are none.";

// multipart body = head | WAV header | audio bytes (file range or memory) | tail
class BodyStream : public Stream {
 public:
  String head, tail; File* f = nullptr; const uint8_t* mem = nullptr; uint32_t len = 0; uint8_t wav[44];
  size_t total = 0, pos = 0; std::function<void(size_t, size_t)> onProg;
  void init() { total = head.length() + 44 + len + tail.length(); pos = 0; }
  int available() override { size_t r = total - pos; return r > 0x7FFFFFFF ? 0x7FFFFFFF : (int)r; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 0; }
  int read() override { char c; return readBytes(&c, 1) == 1 ? (uint8_t)c : -1; }
  size_t readBytes(char* buf, size_t n) {
    size_t done = 0, a = head.length(), b = a + 44, c = b + len;
    while (done < n && pos < total) {
      size_t want = n - done, got;
      if (pos < a) { got = std::min(want, a - pos); memcpy(buf + done, head.c_str() + pos, got); }
      else if (pos < b) { got = std::min(want, b - pos); memcpy(buf + done, wav + (pos - a), got); }
      else if (pos < c) {
        got = std::min(want, c - pos);
        if (mem) memcpy(buf + done, mem + (pos - b), got);
        else { got = f->read((uint8_t*)buf + done, got); if (!got) { pos = total; break; } }
      } else { got = std::min(want, total - pos); memcpy(buf + done, tail.c_str() + (pos - c), got); }
      pos += got; done += got;
    }
    if (onProg) onProg(pos, total);
    return done;
  }
};

// Accepts "host:9000", "http://host:9000", ".../v1", ".../v1/audio/transcriptions" (with or without trailing slash).
static String apiRoot(const WhCfg& c) {          // e.g. http://host:9000/v1
  if (c.svc == 0) return OPENAI_BASE "/v1";
  String s = c.server; s.trim();
  if (s.indexOf("://") < 0) s = "http://" + s;
  while (s.endsWith("/")) s.remove(s.length() - 1);
  int t = s.indexOf("/audio/transcriptions"); if (t >= 0) s = s.substring(0, t);
  int p = s.indexOf('/', s.indexOf("://") + 3);
  if (p < 0) s += "/v1";
  return s;
}
static String sttUrl(const WhCfg& c) { return apiRoot(c) + "/audio/transcriptions"; }
static String baseUrl(const WhCfg& c) { String s = apiRoot(c); int p = s.indexOf('/', s.indexOf("://") + 3); return p < 0 ? s : s.substring(0, p); }
static String hostOf(const WhCfg& c) { String b = baseUrl(c); return b.substring(b.indexOf("://") + 3); }

static int httpDo(const String& url, const WhCfg& c, std::function<int(HTTPClient&)> fn, String& resp) {
  WiFiClientSecure sc; sc.setInsecure();   // TODO: pin a root CA for production
  WiFiClient pc;
  WiFiClient* cl = url.startsWith("https") ? (WiFiClient*)&sc : &pc;
  HTTPClient http; http.setConnectTimeout(15000); http.setTimeout(65000); http.setReuse(false);
  if (!http.begin(*cl, url)) return -1;
  if (c.key.length()) http.addHeader("Authorization", "Bearer " + c.key);
  int code = fn(http);
  resp = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}

static int postAudio(const WhCfg& c, File* f, uint32_t off, uint32_t len, const uint8_t* mem,
                     std::function<void(size_t, size_t)> prog, String& resp) {
  String bnd = "----field" + String(esp_random(), HEX);
  BodyStream bs;
  auto field = [&](const char* name, const String& v) { bs.head += "--" + bnd + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + v + "\r\n"; };
  field("model", c.modelName);
  // gpt-4o(-mini)-transcribe only answer plain json (no segments); whisper-style models give timestamps
  field("response_format", c.modelName.startsWith("gpt-") ? "json" : "verbose_json");
  if (c.lang) field("language", LANG_CODES[c.lang]);
  bs.head += "--" + bnd + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  bs.tail = "\r\n--" + bnd + "--\r\n";
  wavHeader(bs.wav, len); bs.f = f; bs.mem = mem; bs.len = len;
  if (f) f->seek(off);
  bs.init(); bs.onProg = prog;
  return httpDo(sttUrl(c), c, [&](HTTPClient& h) {
    h.addHeader("Content-Type", "multipart/form-data; boundary=" + bnd);
    return h.sendRequest("POST", &bs, bs.total);
  }, resp);
}

// The server's own explanation: OpenAI {"error":{"message"}}, FastAPI {"detail"} (string or list), or plain text.
static String serverMsg(const String& resp) {
  JsonDocument d; String m;
  if (!deserializeJson(d, resp)) {
    if (d["error"]["message"].is<const char*>()) m = d["error"]["message"].as<String>();
    else if (d["error"].is<const char*>()) m = d["error"].as<String>();
    else if (d["detail"].is<const char*>()) m = d["detail"].as<String>();
    else if (d["detail"][0]["msg"].is<const char*>()) m = d["detail"][0]["msg"].as<String>() + " (" + d["detail"][0]["loc"][1].as<String>() + ")";
    else if (d["message"].is<const char*>()) m = d["message"].as<String>();
  } else m = resp;
  m.replace("\n", " "); m.replace("\r", " "); m.trim();
  return m.length() > 140 ? m.substring(0, 137) + "..." : m;
}
static String httpErr(int code, const String& resp) {
  if (code > 0) Serial.printf("[whisper] HTTP %d: %s\n", code, resp.substring(0, 600).c_str());
  if (code <= 0) return "SERVER NOT REACHABLE";
  String m = serverMsg(resp);
  if (code == 401 || code == 403) return m.length() ? "KEY NOT ACCEPTED \xC2\xB7 " + upper(m) : "API KEY NOT ACCEPTED";
  if (code == 429) return "RATE LIMITED \xC2\xB7 TRY LATER";
  return "ERROR " + String(code) + (m.length() ? " \xC2\xB7 " + upper(m) : String());
}

static bool parseStt(const String& resp, uint32_t offset, Transcript& t) {
  JsonDocument filter; filter["text"] = true; filter["segments"][0]["start"] = true; filter["segments"][0]["text"] = true;
  JsonDocument d;
  if (deserializeJson(d, resp, DeserializationOption::Filter(filter))) return false;
  JsonArray segs = d["segments"];
  if (segs.isNull() || !segs.size()) { String tx = d["text"] | ""; tx.trim(); if (tx.length()) t.segs.push_back({offset, tx}); return !d["text"].isNull(); }
  uint32_t ps = 0; String para; bool open = false;
  for (JsonObject s : segs) {   // merge Whisper segments into ~30 s paragraphs
    uint32_t st = offset + (uint32_t)s["start"].as<float>();
    String tx = s["text"].as<String>(); tx.trim(); if (!tx.length()) continue;
    if (!open) { ps = st; para = tx; open = true; }
    else if (st - ps >= 30 || para.length() > 420) { t.segs.push_back({ps, para}); ps = st; para = tx; }
    else { para += " "; para += tx; }
  }
  if (open) t.segs.push_back({ps, para});
  return true;
}

// ---------------- AI (summaries and reports): any OpenAI-compatible chat API ----------------
String aiRoot() {
  String u; { Guard g; u = gSet.aiUrl; } u.trim();
  if (!u.length()) return OPENAI_BASE "/v1";
  if (u.indexOf("://") < 0) u = "http://" + u;
  while (u.endsWith("/")) u.remove(u.length() - 1);
  int t = u.indexOf("/chat/completions"); if (t >= 0) u = u.substring(0, t);
  if (u.indexOf('/', u.indexOf("://") + 3) < 0) u += "/v1";
  return u;
}
String aiModelName() { Guard g; return gSet.aiModel.length() ? gSet.aiModel : String(OPENAI_SUMMARY_MODEL); }
static String aiKey() { Guard g; return gSet.aiKey.length() ? gSet.aiKey : (gSet.aiUrl.length() ? String() : gSet.whKey); }
bool aiReady() { Guard g; return gSet.aiUrl.length() || gSet.aiKey.length() || gSet.whKey.length(); }

static String stripFence(String s) {   // models sometimes wrap output in ``` fences
  s.trim();
  if (s.startsWith("```")) { int nl = s.indexOf('\n'); s = nl >= 0 ? s.substring(nl + 1) : ""; if (s.endsWith("```")) s.remove(s.length() - 3); s.trim(); }
  return s;
}

// One chat completion. Returns "" on success (reply in out) or an error text.
static String aiChat(const String& system, const String& user, bool json, String& out, uint32_t timeoutMs = 240000) {
  if (!aiReady()) return "NO AI SET UP \xC2\xB7 ADD A KEY UNDER REPORT TYPES";
  String root = aiRoot(), key = aiKey();
  JsonDocument req;
  req["model"] = aiModelName();
  if (json && root.startsWith(OPENAI_BASE)) req["response_format"]["type"] = "json_object";
  JsonArray m = req["messages"].to<JsonArray>();
  JsonObject a = m.add<JsonObject>(); a["role"] = "system"; a["content"] = system;
  JsonObject b = m.add<JsonObject>(); b["role"] = "user"; b["content"] = user;
  String body; serializeJson(req, body); req.clear();
  String url = root + "/chat/completions";
  WiFiClientSecure sc; sc.setInsecure(); WiFiClient pc;
  HTTPClient http; http.setConnectTimeout(15000); http.setTimeout(timeoutMs); http.setReuse(false);
  if (!http.begin(url.startsWith("https") ? (WiFiClient&)sc : pc, url)) return "BAD AI ADDRESS";
  http.addHeader("Content-Type", "application/json");
  if (key.length()) http.addHeader("Authorization", "Bearer " + key);
  Serial.printf("[ai] %s · %s · %u bytes\n", url.c_str(), aiModelName().c_str(), (unsigned)body.length());
  int code = http.POST(body);
  String resp = code > 0 ? http.getString() : http.errorToString(code);
  http.end(); body = String();
  if (code != 200) return httpErr(code, resp);
  JsonDocument f; f["choices"][0]["message"]["content"] = true;
  JsonDocument d; if (deserializeJson(d, resp, DeserializationOption::Filter(f))) return "UNEXPECTED AI REPLY";
  out = stripFence(d["choices"][0]["message"]["content"] | "");
  return out.length() ? "" : "EMPTY AI REPLY";
}

static String transcriptText(const Transcript& t) {
  String text; for (auto& s : t.segs) text += "[" + fmtDur(s.first) + "] " + s.second + "\n";
  return text;
}

static String summarise(Transcript& t) {
  String text = transcriptText(t);
  if (!text.length()) return "NOTHING TO SUMMARISE";
  String out, e = aiChat(SUMMARY_PROMPT, text, true, out, 120000);
  if (e.length()) return "SUMMARY FAILED \xC2\xB7 " + e;
  JsonDocument s; if (deserializeJson(s, out)) return "SUMMARY FAILED \xC2\xB7 NOT JSON";
  for (JsonVariant v : s["summary"].as<JsonArray>()) t.sum.push_back(v.as<String>());
  for (JsonVariant v : s["actions"].as<JsonArray>()) t.act.push_back(v.as<String>());
  return "";
}

// Generate one report of `type` for recording `id` and store it. "" = ok.
static String makeReport(uint32_t id, const String& type) {
  ReportType rt; if (!reportType(type, rt)) return "UNKNOWN REPORT TYPE";
  Recording r; { Guard g; Recording* p = recFind(id); if (!p) return "RECORDING NOT FOUND"; r = *p; }
  Transcript t; if (!storageLoadTranscript(id, t) || t.segs.empty()) return "NO TRANSCRIPT YET";
  String user = "Title: " + r.title + "\nDate: " + fmtStamp(r.ts) + "\nDuration: " + fmtDur(r.dur) + "\n";
  if (r.marks.size()) { user += "Marks set during recording at:"; for (auto m : r.marks) user += " " + fmtDur(m); user += "\n"; }
  user += "\nTranscript:\n" + transcriptText(t);
  String out, e = aiChat(rt.prompt, user, false, out);
  if (e.length()) return e;
  RecReport rep; rep.type = rt.id; rep.name = rt.name; rep.text = out; rep.ts = clockValid() ? (uint32_t)time(nullptr) : 0;
  if (!recReportPut(id, rep)) return "COULD NOT SAVE REPORT";
  { Guard g; gRepSeq++; }
  Serial.printf("[ai] report '%s' for %u: %u chars\n", rt.name.c_str(), (unsigned)id, (unsigned)out.length());
  return "";
}

static void repTask(void*) {
  uint32_t id; String type; { Guard g; id = gRep.id; type = gRep.ssid; }
  String e = makeReport(id, type);
  { Guard g; gRep.running = false; if (e.length()) gRep.err = e; else { gRep.done = true; gRep.toast = gRep.det[0] + " READY"; } }
  if (!e.length()) uiToast("REPORT READY");
  uiPoke(); vTaskDelete(nullptr);
}
bool reportStart(uint32_t id, const String& type) {
  Guard g;
  ReportType rt; if (gRep.running || !reportType(type, rt) || !recFind(id)) return false;
  gRep = Job(); gRep.running = true; gRep.kind = 'r'; gRep.id = id; gRep.ssid = type; gRep.det[0] = upper(rt.name);
  xTaskCreatePinnedToCore(repTask, "report", 16384, nullptr, 3, nullptr, 0);
  uiPoke();
  return true;
}

// ---------------- transcription ----------------
static void setRecStatus(uint32_t id, uint8_t st) { Guard g; Recording* r = recFind(id); if (r) { r->status = st; storageSaveMeta(*r); } }

static void runSend() {
  uint32_t id; { Guard g; id = gSend.id; }
  WhCfg c = snap();
  auto step = [&](int s) { Guard g; gSend.step = s; uiPoke(); };
  auto fail = [&](const String& e) { { Guard g; gSend.err = e; } setRecStatus(id, REC_LOCAL); uiPoke(); };

  for (int i = 0; i < 200 && !netOnline(); i++) { if (i == 0) netReconnectSoon(); vTaskDelay(pdMS_TO_TICKS(100)); }
  if (!netOnline()) return fail("NO NETWORK · CHECK WI-FI");
  { Guard g; gSend.det[0] = upper(netSsid()); }
  step(1);

  File f = gFS->open(recWavPath(id));
  if (!f) return fail("AUDIO FILE MISSING");
  uint32_t data = f.size() > 44 ? (f.size() - 44) & ~1u : 0;
  const uint32_t chunk = CHUNK_SECONDS * SAMPLE_RATE * 2;
  uint32_t n = max(1u, (unsigned)((data + chunk - 1) / chunk));
  Transcript t;
  for (uint32_t k = 0; k < n; k++) {
    uint32_t off = 44 + k * chunk, len = min(chunk, data - k * chunk);
    { Guard g; gSend.step = 1; gSend.det[1] = n > 1 ? "PART " + String(k + 1) + " OF " + String(n) : ""; }
    String resp;
    int code = postAudio(c, &f, off, len, nullptr, [&](size_t p, size_t tot) {
      Guard g;
      int pct = data ? (int)(((uint64_t)k * chunk + (uint64_t)len * p / tot) * 100 / data) : 100;
      if (pct != gSend.pct) { gSend.pct = pct; uiPoke(); }
      if (p >= tot && gSend.step == 1) { gSend.step = 2; uiPoke(); }
    }, resp);
    if (code != 200) { f.close(); return fail(httpErr(code, resp)); }
    if (!parseStt(resp, k * CHUNK_SECONDS, t)) { f.close(); return fail("UNEXPECTED REPLY"); }
  }
  f.close();
  { Guard g; gSend.det[1] = String(data / 1000000.0f, 1) + " MB"; gSend.det[2] = upper(c.modelName); }
  step(3);
  String sErr = aiReady() ? summarise(t) : String("SKIPPED \xC2\xB7 NO AI SET UP");
  { Guard g; gSend.det[3] = sErr; }
  if (!storageSaveTranscript(id, t)) return fail("COULD NOT SAVE");
  setRecStatus(id, REC_DONE);
  ReportType rt0; String autoT; { Guard g; Recording* rr = recFind(id); autoT = rr ? rr->rtype : String(); }
  if (autoT == "none") autoT = ""; else if (!autoT.length() || !reportType(autoT, rt0)) autoT = reportAuto();
  ReportType rt;
  if (autoT.length() && aiReady() && reportType(autoT, rt)) {   // the report type chosen to follow every transcription
    { Guard g; gSend.det[3] = upper(rt.name) + "\xE2\x80\xA6"; gRep = Job(); gRep.running = true; gRep.kind = 'r'; gRep.id = id; gRep.ssid = autoT; gRep.det[0] = upper(rt.name); } uiPoke();
    String e = makeReport(id, autoT);
    { Guard g; gRep.running = false; if (e.length()) gRep.err = e; else gRep.done = true; gSend.det[3] = e.length() ? "REPORT FAILED \xC2\xB7 " + e : (sErr.length() ? sErr : upper(rt.name) + " READY"); }
  }
  { Guard g; gSend.step = 4; gSend.done = true; }
  uiToast("TRANSCRIPT READY");
}
static void sendTask(void*) { runSend(); { Guard g; gSend.running = false; } uiPoke(); vTaskDelete(nullptr); }

bool sendStart(uint32_t id) {
  Guard g;
  if (gSend.running || !gSet.whOk) return false;
  Recording* r = recFind(id); if (!r) return false;
  gSend = Job(); gSend.running = true; gSend.kind = 's'; gSend.id = id;
  r->status = REC_SENDING; storageSaveMeta(*r);
  xTaskCreatePinnedToCore(sendTask, "send", 16384, nullptr, 3, nullptr, 0);
  uiPoke();
  return true;
}

void onRecordingSaved(uint32_t id) { if (gSet.autoSend && gSet.whOk) sendStart(id); }

// ---------------- Wi-Fi join ----------------
static SavedNet joinArgs;
static void joinTask(void*) {
  SavedNet n; { Guard g; n = joinArgs; }
  String err;
  bool ok = netJoin(n.ssid, n.pw, [&](int s, const String& d) { Guard g; gProg.det[gProg.step] = s == 1 ? n.sec : d; gProg.step = s; uiPoke(); }, err);
  {
    Guard g;
    if (ok) {
      gProg.det[2] = "ONLINE"; gProg.done = true;
      bool saved = false;
      for (auto& s : gSet.nets) if (s.ssid == n.ssid) { s.pw = n.pw; saved = true; }
      if (!saved) gSet.nets.push_back(n);
      gProg.toast = saved ? "CONNECTED" : "SAVED \xC2\xB7 JOINS AUTOMATICALLY";
    } else gProg.err = err;
    gProg.running = false;
  }
  if (ok) settingsSave();
  uiPoke(); vTaskDelete(nullptr);
}
bool joinStart(const String& ssid, const String& pw, const String& sec) {
  Guard g; if (gProg.running) return false;
  gProg = Job(); gProg.running = true; gProg.kind = 'j'; gProg.ssid = ssid;
  joinArgs = {ssid, pw, sec};
  xTaskCreatePinnedToCore(joinTask, "join", 8192, nullptr, 3, nullptr, 0);
  return true;
}

// ---------------- Whisper test ----------------
static void testTask(void*) {
  WhCfg c = snap();
  auto fail = [&](const String& e) { Guard g; gProg.err = e; };
  auto step = [&](int s, const String& d) { Guard g; gProg.det[gProg.step] = d; gProg.step = s; uiPoke(); };
  do {
    IPAddress ip;
    if (!netOnline()) { fail("NO INTERNET \xC2\xB7 CHECK WI-FI"); break; }
    if (c.svc == 1 && !c.server.length()) { fail("NO SERVER ADDRESS"); break; }
    String host = hostOf(c); int colon = host.indexOf(':'); if (colon >= 0) host = host.substring(0, colon);
    if (!WiFi.hostByName(host.c_str(), ip)) { fail("SERVER NOT FOUND"); break; }
    step(1, upper(hostOf(c)));
    String resp;
    int code = httpDo(apiRoot(c) + "/models", c, [](HTTPClient& h) { return h.GET(); }, resp);
    if (code == 401 || code == 403) { fail(c.svc == 0 || c.key.length() ? httpErr(code, resp) : String("SERVER NEEDS AN API KEY")); break; }
    if (code <= 0 || (c.svc == 0 && code != 200)) { fail(httpErr(code, resp)); break; }
    if (code == 200) {   // does the server know the model? (servers that can load models on demand may not list it)
      JsonDocument f; f["data"][0]["id"] = true; JsonDocument d;
      if (!deserializeJson(d, resp, DeserializationOption::Filter(f))) {
        JsonArray a = d["data"]; bool found = false; String hint;
        for (JsonVariant v : a) { String id = v["id"] | ""; if (id == c.modelName) found = true;
          String l = id; l.toLowerCase(); if (!hint.length() && (l.indexOf("whisper") >= 0 || l.indexOf("transcribe") >= 0)) hint = id; }
        Serial.printf("[whisper] %u models on server, '%s' %s\n", (unsigned)a.size(), c.modelName.c_str(), found ? "listed" : "not listed");
        if (a.size() && !found && c.svc == 0) { fail("MODEL " + upper(c.modelName) + " NOT AVAILABLE"); break; }
        if (a.size() && !found && hint.length()) { Guard g; gProg.det[1] = "MODEL NOT LISTED \xC2\xB7 SERVER HAS " + upper(hint); }
      }
    }
    if (!gProg.det[1].length()) { Guard g; gProg.det[1] = "ACCEPTED"; }
    { Guard g; gProg.step = 2; uiPoke(); }
    const uint32_t len = SAMPLE_RATE * 2 * 2;   // 2 s of quiet noise
    uint8_t* pcm = (uint8_t*)ps_malloc(len);
    if (!pcm) { fail("OUT OF MEMORY"); break; }
    for (uint32_t i = 0; i < len; i += 2) { int16_t v = (int16_t)((esp_random() % 200) - 100); pcm[i] = v; pcm[i + 1] = v >> 8; }
    uint32_t t0 = millis();
    code = postAudio(c, nullptr, 0, len, pcm, nullptr, resp);
    free(pcm);
    if (code != 200) { fail(httpErr(code, resp)); break; }
    { Guard g; gProg.det[2] = "2 S SAMPLE IN " + String((millis() - t0) / 1000.0f, 1) + " S"; gProg.done = true; gSet.whOk = true; gProg.toast = "WHISPER CONNECTED"; }
    settingsSave();
  } while (0);
  { Guard g; gProg.running = false; }
  uiPoke(); vTaskDelete(nullptr);
}
bool whisperTestStart() {
  Guard g; if (gProg.running) return false;
  gProg = Job(); gProg.running = true; gProg.kind = 't';
  gSet.whOk = false;
  xTaskCreatePinnedToCore(testTask, "wtest", 16384, nullptr, 3, nullptr, 0);
  return true;
}
