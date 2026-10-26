#include "app.h"
#include <WiFi.h>
#if __has_include("qrcode.h")
extern "C" {
#include "qrcode.h"
}
#define HAVE_QR 1
#endif

// Screens and behaviour mirror "Field Recorder v2" (device design).
enum Screen : uint8_t { S_HOME, S_SETTINGS, S_WIFI, S_WIFINET, S_WIFISCAN, S_WIFIMETHOD, S_WHISPER, S_HOTSPOT,
                        S_KEYBOARD, S_QR, S_PROG, S_RECORD, S_LIBRARY, S_DETAIL, S_PLAYER, S_SEND, S_READER, S_COUNT };
enum Key : uint8_t { K_UP, K_DOWN, K_PUSH, K_HOLD, K_POKE };

static const int W = 480, BY = 48, BH = 704, HY = 752;
static QueueHandle_t keyQ;
static Screen scr = S_HOME, drawnScr = S_COUNT;
static int homeSel = 0, libSel = 0, libTop = 0, detailSel = 0, netIdx = 0;
static int sel[S_COUNT] = {0};
static uint32_t curId = 0;
struct Dialog { bool on = false; String title, sub, yes; char action = 0; int sel = 0; };
static Dialog dlg;
static ScanNet pending;
static struct { String text; int set = 0; int pos = 6; } kb;
static char qrPurpose = 'w';
static Screen progBack = S_SETTINGS, progNext = S_SETTINGS;
static int readerPage = 0, readerMode = 0, readerModes = 2; static uint32_t readerFor = 0, readerSeq = 0; static int readerBuilt = -1;   // mode 0 transcript, 1 summary, 2+ reports
static std::vector<String> readerNames;
struct PItem { String label; std::vector<String> lines; };
static std::vector<std::vector<PItem>> pages;
static String toastMsg; static uint32_t toastUntil = 0;
static bool wasRecActive = false; static uint32_t lastMinute = 0;

static const char* DAY[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* DAYL[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* MONL[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
static const char* DOT = " \xC2\xB7 ";

void uiPoke() { if (keyQ) { Key k = K_POKE; xQueueSend(keyQ, &k, 0); } }
void uiToast(const String& m) { { Guard g; toastMsg = m; toastUntil = millis() + 2200; } uiPoke(); }
static Recording* cur() { return recFind(curId); }
static bool isMenu(Screen s) { return s >= S_SETTINGS && s <= S_HOTSPOT; }
static Screen backOf(Screen s) {
  switch (s) { case S_SETTINGS: return S_HOME; case S_WIFI: case S_WHISPER: case S_HOTSPOT: return S_SETTINGS;
    case S_WIFINET: case S_WIFISCAN: return S_WIFI; case S_WIFIMETHOD: return S_WIFISCAN; default: return S_HOME; }
}
static void go(Screen s) { scr = s; sel[s] = 0; }
static int wrapI(int i, int n, int d) { return (i + d + n) % n; }
static String mb(uint32_t dur) { return String(dur * 2 * SAMPLE_RATE / 1e6f, 1) + " MB"; }

// ---------------- input ----------------
struct Btn { uint8_t pin; bool stable = false, last = false; uint8_t cnt = 0; };
static void inputTask(void*) {
  Btn up{PIN_BTN_UP}, dn{PIN_BTN_DOWN}, pu{PIN_BTN_PUSH}, bt{PIN_BTN_BOOT};
  Btn* all[] = {&up, &dn, &pu, &bt};
  for (auto* b : all) pinMode(b->pin, INPUT_PULLUP);
  uint32_t puAt = 0, upRep = 0, dnRep = 0; bool holdFired = false;
  auto send = [](Key k) { powerActivity(); xQueueSend(keyQ, &k, 0); };
  for (;;) {
    uint32_t now = millis();
    for (auto* b : all) {
      bool raw = digitalRead(b->pin) == LOW;
      if (raw == b->stable) b->cnt = 0; else if (++b->cnt >= 2) { b->stable = raw; b->cnt = 0; }
    }
    Key kU = BTN_SWAP_UPDOWN ? K_DOWN : K_UP, kD = BTN_SWAP_UPDOWN ? K_UP : K_DOWN;
    if (up.stable && !up.last) { send(kU); upRep = now + 450; } else if (up.stable && now >= upRep) { send(kU); upRep = now + 140; }
    if (dn.stable && !dn.last) { send(kD); dnRep = now + 450; } else if (dn.stable && now >= dnRep) { send(kD); dnRep = now + 140; }
    if (pu.stable && !pu.last) { puAt = now; holdFired = false; }
    if (pu.stable && !holdFired && now - puAt >= HOLD_MS) { holdFired = true; send(K_HOLD); }
    if (!pu.stable && pu.last && !holdFired) send(K_PUSH);
    if (bt.stable && !bt.last) send(K_HOLD);
    for (auto* b : all) b->last = b->stable;
    vTaskDelay(pdMS_TO_TICKS(12));
  }
}

// ---------------- menus ----------------
struct MRow { String id, label, value, tag; bool section = false, tagFill = false, tagLine = false, hasSig = false; int sig = 0; };
struct Menu { String head, sub; std::vector<MRow> rows; };
static MRow row(const String& id, const String& label, const String& value = "") { MRow r; r.id = id; r.label = label; r.value = value; return r; }
static MRow section(const String& label) { MRow r; r.section = true; r.label = label; return r; }
static void tog(MRow& r, bool on) { r.tag = on ? "ON" : "OFF"; r.tagFill = on; r.tagLine = !on; }

static String storageLine() {
  if (!gFS) return "NO STORAGE";
  return String(gSdOk ? "SD CARD " : "INTERNAL ") + String(storageUsed() / 1e9, 1) + " OF " + String(storageTotal() / 1e9, gSdOk ? 0 : 2) + " GB";
}

// Where the web interface is right now. On Wi-Fi: the device's unique name (field-xxxx.local) plus its IP.
// Offline: the device's own network at 192.168.4.1.
static MRow webRow() {
  String u = netWebUrl(); u.replace("http://", "");
  if (netOnline()) return row("web", deviceName() + ".local", String("WEB INTERFACE") + DOT + "OR " + u);
  if (u.length()) return row("web", u, String("WEB INTERFACE") + DOT + "JOIN " + upper(gSet.hsName));
  return row("web", deviceName() + ".local", String("WEB INTERFACE") + DOT + "STARTING");
}
static String webLine() { MRow r = webRow(); return upper(r.label); }

static Menu buildMenu() {
  Menu m; Guard g;
  bool online = netOnline(); String cs = netSsid();
  if (scr == S_SETTINGS) {
    m.head = "Settings"; m.sub = String("FIRMWARE " FW_VERSION) + DOT + storageLine();
    m.rows.push_back(webRow());
    m.rows.push_back(row("wifi", "Wi-Fi", !gSet.wifiOn ? "OFF" : online ? upper(cs) : "NOT CONNECTED"));
    m.rows.push_back(row("whisper", "Whisper", gSet.whOk ? String("CONNECTED") + DOT + (gSet.whSvc == 0 ? "OPENAI API" : gSet.whSvc == 2 ? "MISTRAL" : "SELF-HOSTED") : String("NOT SET UP")));
    m.rows.push_back(row("hotspot", "Local network", gSet.hsOn ? "ALWAYS ON" + String(DOT) + upper(gSet.hsName) : netApAuto() ? "ON WHILE OFFLINE" + String(DOT) + upper(gSet.hsName) : String("AUTOMATIC") + DOT + "ON WHEN OFFLINE"));
    m.rows.push_back(section("RECORDING"));
    MRow a = row("auto", "Send after recording", "TRANSCRIBE AUTOMATICALLY"); tog(a, gSet.autoSend); m.rows.push_back(a);
    m.rows.push_back(row("full", "Clear screen", "FULL REFRESH, REMOVES GHOSTING"));
    m.rows.push_back(section("POWER"));
    int bp = batteryPercent(); bool usb = boardVbus();
    m.rows.push_back(row("sleep", "Sleep after", (gSet.sleepMin ? String(gSet.sleepMin) + " MIN WITHOUT USE" : String("NEVER")) + String(DOT) + "NOT ON CHARGER"));
    m.rows.push_back(row("batt", "Battery", (bp >= 0 ? String(bp) + "%" : String("NO BATTERY")) + (usb ? String(DOT) + (bp >= 0 && bp < 100 ? "CHARGING" : "USB POWER") : String())));
    m.rows.push_back(section("FIRMWARE"));
    if (gUpd.avail) { MRow u = row("update", "Update to " + gUpd.latest, "PUSH TO INSTALL" + String(DOT) + "NOW " FW_VERSION); u.tag = "NEW"; u.tagFill = true; m.rows.push_back(u); }
    else m.rows.push_back(row("update", "Check for update", String("VERSION " FW_VERSION) + DOT + (gUpd.checking ? String("CHECKING\xE2\x80\xA6") : gUpd.err.length() ? gUpd.err : gUpd.checkedAt ? String("UP TO DATE") : String("NOT CHECKED YET"))));
  } else if (scr == S_WIFI) {
    m.head = "Wi-Fi";
    m.sub = !gSet.wifiOn ? String("WI-FI IS OFF. RECORDINGS STAY ON THE DEVICE.") : online ? String("CONNECTED TO ") + upper(cs) : String("NOT CONNECTED");
    MRow t = row("toggle", "Wi-Fi", gSet.wifiOn ? "TURN OFF TO SAVE BATTERY" : "TURN ON TO TRANSCRIBE"); tog(t, gSet.wifiOn); m.rows.push_back(t);
    if (gSet.wifiOn) {
      m.rows.push_back(section(String("SAVED") + DOT + "JOINED IN THIS ORDER"));
      for (size_t i = 0; i < gSet.nets.size(); i++) {
        auto& n = gSet.nets[i]; int sig = netSigFor(n.ssid); bool c = online && cs == n.ssid;
        MRow r = row("net:" + String(i), n.ssid, c ? String("CONNECTED") : sig == 0 ? String("OUT OF RANGE") : String("SAVED") + DOT + n.sec);
        r.hasSig = true; r.sig = sig < 0 ? 0 : sig; if (c) { r.tag = "ON"; r.tagFill = true; }
        m.rows.push_back(r);
      }
      m.rows.push_back(row("add", "Add network", "SEARCH FOR NEARBY NETWORKS"));
    }
  } else if (scr == S_WIFINET) {
    if (netIdx >= (int)gSet.nets.size()) return m;
    auto& n = gSet.nets[netIdx]; int sig = netSigFor(n.ssid); bool c = online && cs == n.ssid;
    static const char* SIG[] = {"WEAK", "WEAK", "FAIR", "GOOD", "STRONG"};
    m.head = n.ssid;
    m.sub = n.sec + DOT + (sig < 0 ? String("SIGNAL UNKNOWN") : sig == 0 ? String("OUT OF RANGE") : String(SIG[sig]) + " SIGNAL") + DOT + String(netIdx + 1) + " OF " + String(gSet.nets.size()) + " IN JOIN ORDER";
    m.rows.push_back(c ? row("disc", "Disconnect", "STAYS SAVED") : row("conn", "Connect", sig == 0 ? "NOT IN RANGE RIGHT NOW" : "JOIN NOW"));
    if (netIdx > 0) m.rows.push_back(row("top", "Join this first", "MOVE TO THE TOP OF THE LIST"));
    m.rows.push_back(row("forget", "Forget network", "REMOVES THE SAVED PASSWORD"));
  } else if (scr == S_WIFISCAN) {
    auto res = netScan(); bool busy = netScanning();
    m.head = "Nearby networks";
    m.sub = busy ? String("SEARCHING") + String("...").substring(0, 1 + (millis() / 700) % 3) : String(res.size()) + " NETWORKS FOUND" + DOT + "STRONGEST FIRST";
    if (!busy) for (size_t i = 0; i < res.size(); i++) {
      auto& n = res[i]; bool saved = false; for (auto& s : gSet.nets) if (s.ssid == n.ssid) saved = true;
      MRow r = row("scan:" + String(i), n.ssid, saved ? String("SAVED") + DOT + "JOINS WITHOUT PASSWORD" : n.sec == "OPEN" ? String("OPEN") + DOT + "NO PASSWORD" : n.sec + DOT + "NEEDS PASSWORD");
      r.hasSig = true; r.sig = n.sig; m.rows.push_back(r);
    }
    if (!busy) m.rows.push_back(row("rescan", "Search again"));
  } else if (scr == S_WIFIMETHOD) {
    m.head = pending.ssid; m.sub = "THIS NETWORK NEEDS A PASSWORD. HOW DO YOU WANT TO ENTER IT?";
    m.rows.push_back(row("phone", "Type on phone", String("SCAN A CODE") + DOT + "EASIEST"));
    m.rows.push_back(row("here", "Type here", "PICK LETTERS WITH THE ROCKER"));
  } else if (scr == S_WHISPER) {
    bool cred = whHasCred(), srv = gSet.whSvc == 1;
    m.head = "Whisper";
    m.sub = gSet.whOk ? String("CONNECTED") + DOT + "READY TO TRANSCRIBE" : cred ? String("NOT TESTED YET") + DOT + "RUN A TEST" : String("NOT SET UP") + DOT + (srv ? "NEEDS A SERVER ADDRESS" : "NEEDS AN API KEY");
    m.rows.push_back(row("svc", "Service", gSet.whSvc == 0 ? "OPENAI WHISPER API" : gSet.whSvc == 2 ? "MISTRAL VOXTRAL" + String(DOT) + "EU" : "SELF-HOSTED WHISPER SERVER"));
    MRow c = row("cred", srv ? "Server" : "API key", cred ? (srv ? upper(gSet.whServer) : maskKey(whApiKey())) : String("NOT SET") + DOT + "ADD WITH YOUR PHONE");
    if (!cred) { c.tag = "SET UP"; c.tagLine = true; }
    m.rows.push_back(c);
    if (gSet.whSvc == 1) {
      m.rows.push_back(row("srvkey", "API key", gSet.whSrvKey.length() ? maskKey(gSet.whSrvKey) + DOT + "PUSH TO CHANGE" : String("OPTIONAL") + DOT + "ADD WITH YOUR PHONE"));
    }
    m.rows.push_back(row("model", "Model", upper(whModelName()) + DOT + (whModelTyped().length() ? "TYPED" : "DEFAULT") + DOT + "PUSH TO TYPE"));
    if (gSet.whSvc == 2) { MRow s = row("spk", "Label speakers", gSet.whSpk ? "SPEAKER 1, 2" + String(DOT) + "NAME THEM IN THE WEB PAGE" : "OFF"); tog(s, gSet.whSpk); m.rows.push_back(s); }
    m.rows.push_back(row("lang", "Language", whDiarize() ? String("DETECTED") + DOT + "WHILE LABELLING SPEAKERS" : String(LANG_NAMES[gSet.whLang])));
    m.rows.push_back(row("test", "Test connection", gSet.whOk ? "LAST TEST PASSED" : "SENDS A 2-SECOND SAMPLE"));
  } else if (scr == S_HOTSPOT) {
    bool on = gSet.hsOn || netApAuto();
    m.head = "Local network";
    m.sub = on ? String("JOIN ") + upper(gSet.hsName) + " FROM A PHONE OR LAPTOP, THEN OPEN 192.168.4.1 IN A BROWSER."
               : String("THE DEVICE CREATES ITS OWN WI-FI NETWORK. JOIN IT TO USE EVERYTHING FROM A BROWSER.");
    MRow t = row("toggle", "Local network", gSet.hsOn ? "ALWAYS ON" + String(DOT) + "WI-FI STAYS CONNECTED" : netApAuto() ? "ON WHILE WI-FI IS OFFLINE" : "AUTOMATIC" + String(DOT) + "ON WHEN WI-FI IS OFFLINE"); tog(t, gSet.hsOn); m.rows.push_back(t);
    if (on) {
      m.rows.push_back(section("JOIN WITH"));
      m.rows.push_back(row("name", gSet.hsName, "NETWORK NAME"));
      m.rows.push_back(row("pw", gSet.hsPw, String("PASSWORD") + DOT + "PUSH FOR A NEW ONE"));
      m.rows.push_back(row("addr", "192.168.4.1", String("ADDRESS") + DOT + "OR " + upper(deviceName()) + ".LOCAL"));
    }
  }
  return m;
}

// ---------------- flows ----------------
static void startJoin(const String& ssid, const String& pw, const String& sec, Screen back) {
  if (!joinStart(ssid, pw, sec)) return uiToast("BUSY");
  progBack = back; progNext = S_WIFI; scr = S_PROG;
}
static void startTest() {
  if (!whisperTestStart()) return uiToast("BUSY");
  progBack = S_WHISPER; progNext = S_WHISPER; scr = S_PROG;
}
static void startQR(char purpose) {
  qrPurpose = purpose;
  { Guard g; gPhone = PhoneSetup(); gPhone.active = true; gPhone.purpose = purpose; gPhone.ssid = pending.ssid; gPhone.sec = pending.sec; }
  if (purpose == 'w') netSetApTemp(true);
  scr = S_QR;
}
static void endQR() { { Guard g; gPhone.active = false; } netSetApTemp(false); }
static void openSend(uint32_t id) {
  if (!gSet.whOk) { uiToast("SET UP WHISPER FIRST"); return go(S_WHISPER); }
  if (!sendStart(id)) { Guard g; if (!(gSend.running && gSend.id == id)) return uiToast("ANOTHER SEND IS RUNNING"); }
  scr = S_SEND;
}

static void menuAct(const String& id) {
  if (scr == S_SETTINGS) {
    if (id == "web") { if (netOnline()) return uiToast(webLine()); return go(S_HOTSPOT); }
    if (id == "wifi") { go(S_WIFI); netScanStart(); return; }
    if (id == "whisper") return go(S_WHISPER);
    if (id == "hotspot") return go(S_HOTSPOT);
    if (id == "auto") { gSet.autoSend = !gSet.autoSend; settingsSave(); return; }
    if (id == "full") return dispRequestFull("MANUAL");
    if (id == "sleep") { static const uint16_t S[] = {2, 5, 10, 15, 30, 60, 0}; int i = 0; while (i < 6 && S[i] != gSet.sleepMin) i++; gSet.sleepMin = S[(i + 1) % 7]; settingsSave(); return uiToast(gSet.sleepMin ? "SLEEPS AFTER " + String(gSet.sleepMin) + " MIN" : String("NEVER SLEEPS")); }
    if (id == "batt") return;
    if (id == "update") {
      if (!gUpd.avail) { if (!otaCheckStart() && gUpd.err.length()) uiToast(gUpd.err); return; }
      dlg = Dialog(); dlg.on = true; dlg.title = "Install " + gUpd.latest + "?"; dlg.sub = "RESTARTS WHEN DONE" + String(DOT) + "ABOUT 1 MIN"; dlg.yes = "Install"; dlg.action = 'u'; return;
    }
  }
  if (scr == S_WIFI) {
    if (id == "toggle") { bool on = !gSet.wifiOn; netSetWifiOn(on); uiToast(on ? "WI-FI ON" : "WI-FI OFF"); if (on) netScanStart(); return; }
    if (id == "add") { go(S_WIFISCAN); netScanStart(); return; }
    if (id.startsWith("net:")) { netIdx = id.substring(4).toInt(); return go(S_WIFINET); }
  }
  if (scr == S_WIFINET) {
    if (netIdx >= (int)gSet.nets.size()) return;
    SavedNet n = gSet.nets[netIdx];
    if (id == "conn") { if (netSigFor(n.ssid) == 0) return uiToast("NOT IN RANGE"); return startJoin(n.ssid, n.pw, n.sec, S_WIFINET); }
    if (id == "disc") { netDisconnect(); return uiToast("DISCONNECTED"); }
    if (id == "top") { { Guard g; gSet.nets.erase(gSet.nets.begin() + netIdx); gSet.nets.insert(gSet.nets.begin(), n); } settingsSave(); netIdx = 0; sel[S_WIFINET] = 0; return uiToast("JOINS FIRST NOW"); }
    if (id == "forget") { dlg = Dialog(); dlg.on = true; dlg.title = "Forget network?"; dlg.sub = upper(n.ssid); dlg.yes = "Forget"; dlg.action = 'f'; return; }
  }
  if (scr == S_WIFISCAN) {
    if (id == "rescan") return netScanStart();
    auto res = netScan(); int i = id.substring(5).toInt(); if (i >= (int)res.size()) return;
    ScanNet n = res[i];
    for (auto& s : gSet.nets) if (s.ssid == n.ssid) return startJoin(s.ssid, s.pw, s.sec, S_WIFISCAN);
    if (n.sec == "OPEN") return startJoin(n.ssid, "", "OPEN", S_WIFISCAN);
    pending = n; return go(S_WIFIMETHOD);
  }
  if (scr == S_WIFIMETHOD) {
    if (id == "phone") return startQR('w');
    if (id == "here") { kb.text = ""; kb.set = 0; kb.pos = 6; scr = S_KEYBOARD; return; }
  }
  if (scr == S_WHISPER) {
    if (id == "svc") { gSet.whSvc = (gSet.whSvc + 1) % 3; gSet.whOk = false; settingsSave(); return; }
    if (id == "spk") { gSet.whSpk = !gSet.whSpk; settingsSave(); return; }
    if (id == "model") { if (!netOnline()) { uiToast("CONNECT TO WI-FI FIRST"); return go(S_WIFI); } return startQR('M'); }
    if (id == "lang") { gSet.whLang = (gSet.whLang + 1) % 5; settingsSave(); return; }
    if (id == "cred") { if (!netOnline()) { uiToast("CONNECT TO WI-FI FIRST"); return go(S_WIFI); } return startQR('k'); }
    if (id == "srvkey") { if (!netOnline()) { uiToast("CONNECT TO WI-FI FIRST"); return go(S_WIFI); } return startQR('K'); }
    if (id == "test") {
      if (!whHasCred()) return uiToast(gSet.whSvc == 1 ? "ADD A SERVER FIRST" : "ADD AN API KEY FIRST");
      return startTest();
    }
  }
  if (scr == S_HOTSPOT) {
    if (id == "toggle") { bool on = !gSet.hsOn; netSetHotspot(on); return uiToast(on ? "LOCAL NETWORK ON" : "LOCAL NETWORK OFF"); }
    if (id == "pw") { gSet.hsPw = randomPw(); settingsSave(); netRestartAp(); return uiToast("NEW PASSWORD SET"); }
  }
}

static void dialogAct() {
  if (dlg.action == 'd') {
    playStop(); storageDelete(curId);
    libSel = min(libSel, max(0, (int)gRecs.size() - 1)); libTop = min(libTop, libSel);
    uiToast("RECORDING DELETED"); scr = S_LIBRARY;
  } else if (dlg.action == 'f' && netIdx < (int)gSet.nets.size()) {
    String ssid = gSet.nets[netIdx].ssid;
    { Guard g; gSet.nets.erase(gSet.nets.begin() + netIdx); }
    settingsSave();
    if (netSsid() == ssid) netDisconnect();
    uiToast("NETWORK FORGOTTEN"); scr = S_WIFI;
  } else if (dlg.action == 'u') {
    if (!otaInstallStart()) { Guard g; uiToast(gUpd.err); }
  }
  dlg.on = false;
}

// keyboard
static const char* SETS[] = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", "!@#$%&*-_.,?+=/:;()"};
static const char* SETN[] = {"abc", "ABC", "123", "#+="};
struct KItem { char k; String label, cap; int to = 0; };
static std::vector<KItem> kbItems() {
  std::vector<KItem> it = {{'d', "Done", "SAVE AND CONNECT"}, {'x', "Delete", "REMOVE LAST"}, {' ', "Space", ""}};
  for (int i = 0; i < 4; i++) if (i != kb.set) { KItem s{'s', SETN[i], "SWITCH SET"}; s.to = i; it.push_back(s); }
  for (const char* p = SETS[kb.set]; *p; p++) it.push_back({'c', String(*p), ""});
  return it;
}

// reader
static String plainMd(String l) {   // strip Markdown emphasis for the e-paper
  l.replace("**", ""); l.replace("__", ""); l.replace("`", ""); l.trim(); return l;
}
static void buildPages(uint32_t id, int mode) {
  pages.clear();
  const int widthPx = W - 72, avail = BH - 22 - 40 - 18, lineH = max(textLineH(F_SERIF_S), textAsc(F_SERIF_S) * 16 / 10), labH = textAsc(F_MONO_S) + 9, gap = 16;
  struct Src { String label, cont, text; };
  std::vector<Src> src;
  Transcript t; if (mode < 2) storageLoadTranscript(id, t);
  auto reps = recReports(id);
  readerNames.clear(); readerNames.push_back("TRANSCRIPT"); readerNames.push_back("SUMMARY"); for (auto& r : reps) readerNames.push_back(upper(r.name));
  readerModes = readerNames.size();
  if (mode == 1) {
    for (size_t i = 0; i < t.sum.size(); i++) src.push_back({i ? "" : "SUMMARY", "SUMMARY", t.sum[i]});
    for (size_t i = 0; i < t.act.size(); i++) src.push_back({i ? "" : "ACTION ITEMS", "ACTION ITEMS", t.act[i]});
    if (src.empty()) src.push_back({"SUMMARY", "", "No summary for this recording."});
  } else if (mode >= 2 && mode - 2 < (int)reps.size()) {
    // Markdown report: headings become labels, bullets get a dash, table rows are joined with dots
    String label, cont; int from = 0; const String& x = reps[mode - 2].text;
    while (from <= (int)x.length()) {
      int nl = x.indexOf('\n', from); if (nl < 0) nl = x.length();
      String l = x.substring(from, nl); from = nl + 1; l.trim();
      if (!l.length()) continue;
      if (l.startsWith("#")) { while (l.startsWith("#")) l.remove(0, 1); label = upper(plainMd(l)); cont = label; continue; }
      if (l.startsWith("|")) {
        if (l.indexOf("---") >= 0) continue;
        String o; int p = 1; while (p < (int)l.length()) { int q = l.indexOf('|', p); if (q < 0) q = l.length(); String c = l.substring(p, q); c.trim(); if (c.length()) { if (o.length()) o += DOT; o += c; } p = q + 1; }
        l = o;
      } else if (l.startsWith("- ") || l.startsWith("* ")) l = "\xE2\x80\x94 " + l.substring(2);
      if (l == "---") continue;
      src.push_back({label, cont, plainMd(l)}); label = "";
    }
    if (src.empty()) src.push_back({"", "", "This report is empty."});
  } else for (size_t i = 0; i < t.segs.size(); i++) {
    String n = speakerName(t, i), l = fmtDur(t.segs[i].first) + (n.length() ? String(DOT) + upper(n) : String());
    src.push_back({l, l, t.segs[i].second});
  }
  std::vector<PItem> page; int used = 0;
  for (auto& s : src) {
    auto lines = wrapText(s.text, F_SERIF_S, widthPx);
    PItem it; it.label = s.label; int need = (it.label.length() ? labH : 0);
    for (size_t i = 0; i < lines.size(); i++) {
      if (used + need + lineH > avail && (page.size() || it.lines.size())) {
        if (it.lines.size()) page.push_back(it);
        pages.push_back(page); page.clear(); used = 0;
        it = PItem(); it.label = s.cont.length() ? s.cont + DOT + "CONT." : ""; need = it.label.length() ? labH : 0;
      }
      it.lines.push_back(lines[i]); need += lineH;
    }
    page.push_back(it); used += need + gap;
  }
  if (page.size()) pages.push_back(page);
  if (pages.empty()) pages.push_back({});
}

// ---------------- key handling ----------------
static void handleKey(Key k) {
  { Guard g; if (gUpd.installing) return; }
  if (dlg.on) {
    if (k == K_UP || k == K_DOWN) dlg.sel = 1 - dlg.sel;
    else if (k == K_HOLD) dlg.on = false;
    else if (k == K_PUSH) { if (dlg.sel == 0) dlg.on = false; else dialogAct(); }
    return;
  }
  if (isMenu(scr)) {
    Menu m = buildMenu(); std::vector<int> selectable;
    for (size_t i = 0; i < m.rows.size(); i++) if (!m.rows[i].section) selectable.push_back(i);
    if (k == K_HOLD) { scr = backOf(scr); return; }
    if (selectable.empty()) return;
    int c = min(sel[scr], (int)selectable.size() - 1);
    if (k == K_UP || k == K_DOWN) sel[scr] = wrapI(c, selectable.size(), k == K_UP ? -1 : 1);
    else if (k == K_PUSH) menuAct(m.rows[selectable[c]].id);
    return;
  }
  switch (scr) {
    case S_HOME:
      if (k == K_UP || k == K_DOWN) homeSel = wrapI(homeSel, 3, k == K_UP ? -1 : 1);
      else if (k == K_PUSH) {
        if (homeSel == 0) { if (recStart()) scr = S_RECORD; else uiToast(gFS ? "CANNOT START RECORDING" : "NO STORAGE"); }
        else go(homeSel == 1 ? S_LIBRARY : S_SETTINGS);
      }
      break;
    case S_KEYBOARD: {
      auto it = kbItems();
      if (k == K_HOLD) { scr = S_WIFIMETHOD; break; }
      if (k == K_UP || k == K_DOWN) { kb.pos = wrapI(kb.pos, it.size(), k == K_UP ? -1 : 1); break; }
      if (k != K_PUSH) break;
      KItem& i = it[kb.pos];
      if (i.k == 'c') kb.text += i.label;
      else if (i.k == ' ') kb.text += " ";
      else if (i.k == 'x') { if (kb.text.length()) kb.text.remove(kb.text.length() - 1); }
      else if (i.k == 's') { kb.set = i.to; kb.pos = 6; }
      else if (i.k == 'd') { if (kb.text.length() < 8) uiToast("AT LEAST 8 CHARACTERS"); else startJoin(pending.ssid, kb.text, pending.sec, S_WIFIMETHOD); }
      break;
    }
    case S_QR:
      if (k == K_HOLD) { endQR(); scr = qrPurpose == 'w' ? S_WIFIMETHOD : S_WHISPER; }
      else if (k == K_PUSH && qrPurpose == 'w') { endQR(); kb.text = ""; kb.set = 0; kb.pos = 6; scr = S_KEYBOARD; }
      break;
    case S_PROG: {
      bool done, err; { Guard g; done = gProg.done; err = !gProg.running && !gProg.done; }
      if (k == K_HOLD) scr = done ? progNext : progBack;
      else if (k == K_PUSH) {
        if (done) scr = progNext;
        else if (err) { if (gProg.kind == 't') startTest(); else scr = progBack; }
      }
      break;
    }
    case S_RECORD:
      if (k == K_PUSH) recTogglePause();
      else if (k == K_UP || k == K_DOWN) { recMark(); RecLive l = recLive(); uiToast("MARK " + String(l.marks.size()) + DOT + fmtDur(l.t)); }
      else if (k == K_HOLD) {
        uint32_t id = recStop(); wasRecActive = false;
        Recording* r = recFind(id);
        if (r) { uiToast("SAVED" + String(DOT) + fmtDur(r->dur)); curId = id; libSel = libTop = 0; scr = S_DETAIL; detailSel = 1; onRecordingSaved(id); if (gSend.running && gSend.id == id) scr = S_SEND; }
        else scr = S_HOME;
      }
      break;
    case S_LIBRARY: {
      int n = gRecs.size();
      if (k == K_HOLD) { scr = S_HOME; break; }
      if (!n) break;
      if (k == K_UP || k == K_DOWN) { libSel = wrapI(libSel, n, k == K_UP ? -1 : 1); if (libSel < libTop) libTop = libSel; if (libSel >= libTop + 7) libTop = libSel - 6; }
      else if (k == K_PUSH) { curId = gRecs[libSel].id; scr = S_DETAIL; detailSel = 0; }
      break;
    }
    case S_DETAIL: {
      Recording* r = cur(); if (!r) { scr = S_LIBRARY; break; }
      if (k == K_HOLD) { scr = S_LIBRARY; break; }
      if (k == K_UP || k == K_DOWN) { detailSel = wrapI(detailSel, 3, k == K_UP ? -1 : 1); break; }
      if (k != K_PUSH) break;
      if (detailSel == 0) { if (playStart(r->id, 0)) scr = S_PLAYER; else uiToast("CANNOT PLAY"); }
      else if (detailSel == 1) {
        if (r->status == REC_DONE) { readerPage = 0; readerMode = 0; readerBuilt = -1; scr = S_READER; }
        else if (r->status == REC_SENDING) scr = S_SEND;
        else openSend(r->id);
      } else { dlg = Dialog(); dlg.on = true; dlg.title = "Delete recording?"; dlg.sub = upper(r->title); dlg.yes = "Delete"; dlg.action = 'd'; }
      break;
    }
    case S_PLAYER:
      if (k == K_HOLD) { playStop(); scr = S_DETAIL; }
      else if (k == K_PUSH) { PlayLive p = playLive(); if (!p.active) playStart(curId, 0); else playToggle(); }
      else if (k == K_UP) playSeek(-10);
      else if (k == K_DOWN) playSeek(10);
      break;
    case S_SEND: {
      bool done, err; uint32_t id; { Guard g; done = gSend.done; err = gSend.err.length() > 0; id = gSend.id; }
      if (k == K_HOLD) { scr = S_DETAIL; detailSel = 1; }
      else if (k == K_PUSH) {
        if (done) { curId = id; readerPage = 0; readerMode = 0; readerBuilt = -1; scr = S_READER; }
        else if (err) openSend(id);
      }
      break;
    }
    case S_READER: {
      int n = pages.size();
      if (k == K_HOLD) scr = S_DETAIL;
      else if (k == K_UP) readerPage = max(0, readerPage - 1);
      else if (k == K_DOWN) readerPage = min(n - 1, readerPage + 1);
      else if (k == K_PUSH) { readerMode = (readerMode + 1) % max(2, readerModes); readerPage = 0; readerBuilt = -1; }
      break;
    }
    default: break;
  }
}

// ---------------- drawing ----------------
static void marker(int x, int cy, int s = 18) { fillR(x + (36 - s) / 2, cy - s / 2, s, s); }
static int mid(int y, int h, FontId f) { return y + (h - textAsc(f)) / 2; }

static String crumbs() {
  std::vector<String> segs; Recording* r = cur(); String rt = r ? upper(r->title) : "";
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  String net = netIdx < (int)gSet.nets.size() ? upper(gSet.nets[netIdx].ssid) : "";
  String ps = upper(pending.ssid);
  switch (scr) {
    case S_HOME: segs = {clockValid() ? String(DAY[tm.tm_wday]) + " " + tm.tm_mday + " " + MON[tm.tm_mon] : String("FIELD RECORDER")}; break;
    case S_RECORD: segs = {"RECORD"}; break;
    case S_LIBRARY: segs = {"LIBRARY"}; break;
    case S_DETAIL: segs = {"LIBRARY", rt}; break;
    case S_PLAYER: segs = {"LIBRARY", rt, "LISTEN"}; break;
    case S_SEND: segs = {"LIBRARY", rt, "SEND"}; break;
    case S_READER: segs = {"LIBRARY", rt, readerMode < (int)readerNames.size() ? readerNames[readerMode] : String("TRANSCRIPT")}; break;
    case S_SETTINGS: segs = {"SETTINGS"}; break;
    case S_WIFI: segs = {"SETTINGS", "WI-FI"}; break;
    case S_WIFINET: segs = {"SETTINGS", "WI-FI", net}; break;
    case S_WIFISCAN: segs = {"SETTINGS", "WI-FI", "ADD"}; break;
    case S_WIFIMETHOD: segs = {"SETTINGS", "WI-FI", "ADD", ps}; break;
    case S_KEYBOARD: segs = {"SETTINGS", "WI-FI", "ADD", ps, "PASSWORD"}; break;
    case S_WHISPER: segs = {"SETTINGS", "WHISPER"}; break;
    case S_HOTSPOT: segs = {"SETTINGS", "LOCAL NETWORK"}; break;
    case S_QR: segs = qrPurpose != 'w' ? std::vector<String>{"SETTINGS", "WHISPER", "PHONE"} : std::vector<String>{"SETTINGS", "WI-FI", ps, "PHONE"}; break;
    case S_PROG: segs = gProg.kind == 't' ? std::vector<String>{"SETTINGS", "WHISPER", "TEST"} : std::vector<String>{"SETTINGS", "WI-FI", "CONNECT"}; break;
    default: break;
  }
  if (scr == S_HOME && gUpd.avail) segs.push_back("UPDATE " + gUpd.latest);
  if (dlg.on) segs.push_back("CONFIRM");
  for (auto& s : segs) if (s.length() > 11) s = s.substring(0, 10) + "\xE2\x80\xA6";
  auto join = [&](size_t from) { String o; for (size_t i = from; i < segs.size(); i++) { if (o.length()) o += " \xE2\x80\xBA "; o += segs[i]; } return o; };
  size_t from = 0; String out = join(0);
  while (segs.size() - from > 1 && textW(out, F_MONO_S, 1) > 20 * 11) { from++; out = "\xE2\x80\xA6 \xE2\x80\xBA " + join(from); }
  return out;
}

static void drawHeader() {
  int x = W - 22, ty = mid(0, 46, F_MONO_S);
  fillR(x - 3, 21, 3, 5); x -= 3 + 26;
  boxR(x, 17, 26, 13, 2);
  int pct = batteryPercent(); fillR(x + 4, 21, pct < 0 ? 18 : 18 * pct / 100, 5);
  x -= 14;
  auto put = [&](const String& s) { int w = textW(s, F_MONO_S, 2); x -= w; drawText(x, ty, s, F_MONO_S, 2); x -= 14; };
  put(netOnline() ? "WI-FI" : "OFFLINE");
  if (netApOn()) put("AP");
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  char clk[8]; snprintf(clk, sizeof clk, "%02d:%02d", tm.tm_hour, tm.tm_min);
  if (clockValid()) put(clk);
  drawText(22, ty, crumbs(), F_MONO_S, 1, x - 22);
  fillR(0, 46, W, 2);
}

static void drawHints(const std::vector<String>& h) {
  fillR(0, HY, W, 2);
  int ty = mid(HY + 2, 46, F_MONO_S), total = 0;
  for (auto& s : h) total += textW(s, F_MONO_S, 2);
  int gap = h.size() > 1 ? (W - 44 - total) / (int)(h.size() - 1) : 0, x = 22;
  for (auto& s : h) { x += drawText(x, ty, s, F_MONO_S, 2) + gap; }
}

static void drawHome() {
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  String date = clockValid() ? String(DAYL[tm.tm_wday]) + ", " + tm.tm_mday + " " + MONL[tm.tm_mon] : "Clock not set";
  int y = BY + 40;
  for (auto& l : wrapText(date, F_SERIF_L, W - 72 - 32)) { drawText(72, y, l, F_SERIF_L); y += textAsc(F_SERIF_L) + 14; }
  int n = gRecs.size(), local = 0; for (auto& r : gRecs) if (r.status == REC_LOCAL) local++;
  String metas[3] = {"PUSH TO START",
                     String(n) + (n == 1 ? " RECORDING" : " RECORDINGS") + (local ? String(DOT) + String(local) + " NOT TRANSCRIBED" : String()),
                     (netOnline() ? upper(netSsid()) : String("OFFLINE")) + DOT + (gSet.whOk ? "WHISPER READY" : "WHISPER NOT SET UP")};
  const char* labels[3] = {"Record", "Library", "Settings"};
  int y0 = BY + BH - 44 - 3 * 136, la = textAsc(F_SERIF_XL), ma = textAsc(F_MONO_S);
  for (int i = 0; i < 3; i++) {
    int ty = y0 + i * 136 + (136 - (la + 16 + ma)) / 2;
    if (homeSel == i) marker(22, ty + la / 2);
    drawText(72, ty, labels[i], F_SERIF_XL);
    drawText(72, ty + la + 16, metas[i], F_MONO_S, 2, W - 72 - 32);
  }
}

static void sigBars(int right, int cy, int sig) {
  static const int H[] = {6, 10, 14, 18};
  int x = right - (4 * 6 + 3 * 3);
  for (int j = 0; j < 4; j++) { int h = H[j], yy = cy + 9 - h; if (j < sig) fillR(x, yy, 6, h); else boxR(x, yy, 6, h, 2); x += 9; }
}

static void drawMenu() {
  Menu m = buildMenu();
  int y = BY + 34;
  for (auto& l : wrapText(m.head, F_SERIF_L, 378)) { drawText(72, y, l, F_SERIF_L, 0, 378); y += textAsc(F_SERIF_L) + 14; }
  y += 4;
  for (auto& l : wrapText(m.sub, F_MONO_S, 378, 1)) { drawText(72, y, l, F_MONO_S, 1); y += textAsc(F_MONO_S) + 9; }
  y += 10;
  std::vector<int> selectable; for (size_t i = 0; i < m.rows.size(); i++) if (!m.rows[i].section) selectable.push_back(i);
  int selRow = selectable.empty() ? -1 : selectable[min(sel[scr], (int)selectable.size() - 1)];
  const int bottom = BY + BH - 34, avail = bottom - y;
  auto hOf = [&](int i) { return m.rows[i].section ? 48 : 82; };
  int top = 0;
  while (selRow >= 0 && top < selRow) { int h = 0; for (int i = top; i <= selRow; i++) h += hOf(i); if (h <= avail) break; top++; }
  int i = top;
  for (; i < (int)m.rows.size(); i++) {
    MRow& r = m.rows[i]; int h = hOf(i);
    if (y + h > bottom) break;
    if (r.section) { drawText(72, y + h - 9 - textAsc(F_MONO_S), r.label, F_MONO_S, 2); y += h; continue; }
    if (i == selRow) marker(22, y + 41);
    fillR(72, y + 81, W - 30 - 72, 1);
    int rx = W - 30;
    if (r.hasSig) { sigBars(rx, y + 41, r.sig); rx -= 33 + 14; }
    if (r.tagFill || r.tagLine) {
      int tw = textW(r.tag, F_MONO_S, 2), bw = tw + 16, bh = textAsc(F_MONO_S) + 12, bx = rx - bw, by = y + 41 - bh / 2;
      if (r.tagFill) { fillR(bx, by, bw, bh); drawText(bx + 8, by + 6, r.tag, F_MONO_S, 2, 0, 0); }
      else { boxR(bx, by, bw, bh, 2); drawText(bx + 8, by + 6, r.tag, F_MONO_S, 2); }
      rx = bx - 14;
    }
    int la = textAsc(F_SERIF_M), va = textAsc(F_MONO_S);
    int ty = r.value.length() ? y + (82 - (la + 10 + va)) / 2 : y + (82 - la) / 2;
    drawText(72, ty, r.label, F_SERIF_M, 0, rx - 72);
    if (r.value.length()) drawText(72, ty + la + 10, r.value, F_MONO_S, 1, rx - 72);
    y += h;
  }
  if (top > 0 || i < (int)m.rows.size()) drawText(72, bottom + 10, i < (int)m.rows.size() ? "\xE2\x96\xBC MORE" : "\xE2\x96\xB2 MORE", F_MONO_S, 2);
}

static void drawKeyboard() {
  int y = BY + 24;
  drawText(30, y, "PASSWORD FOR " + upper(pending.ssid), F_MONO_S, 2, W - 60);
  y += textAsc(F_MONO_S) + 12;
  String t = kb.text; if (t.length() > 17) t = "\xE2\x80\xA6" + t.substring(t.length() - 16);
  int tw = drawText(30, y + (58 - textAsc(F_MONO_L)) / 2, t, F_MONO_L);
  fillR(30 + tw + 4, y + 12, 16, 34);
  fillR(30, y + 55, W - 60, 3);
  y += 58 + 10;
  drawText(30, y, String(kb.text.length()) + (kb.text.length() == 1 ? " CHARACTER" : " CHARACTERS") + (kb.text.length() < 8 ? String(DOT) + "NEEDS AT LEAST 8" : String()), F_MONO_S, 2);
  y += textAsc(F_MONO_S) + 6;
  auto it = kbItems();
  static const int H[] = {50, 60, 72, 128, 72, 60, 50};
  static const FontId CF[] = {F_MONO_M, F_MONO_L, F_MONO_L, F_MONO_XL, F_MONO_L, F_MONO_L, F_MONO_M};
  static const FontId WF[] = {F_SERIF_S, F_SERIF_S, F_SERIF_M, F_SERIF_L, F_SERIF_M, F_SERIF_S, F_SERIF_S};
  int total = 0; for (int h : H) total += h;
  int wy = y + (BY + BH - 18 - y - total) / 2;
  for (int o = -3; o <= 3; o++) {
    KItem& k = it[(kb.pos + o + it.size() * 2) % it.size()];
    int h = H[o + 3]; FontId f = k.k == 'c' ? CF[o + 3] : WF[o + 3];
    if (o == 0) boxR(30, wy, W - 60, h, 3);
    drawText(52, mid(wy, h, f), k.label, f);
    if (o == 0 && k.cap.length()) { int cw = textW(k.cap, F_MONO_S, 2); drawText(W - 52 - cw, mid(wy, h, F_MONO_S), k.cap, F_MONO_S, 2); }
    wy += h;
  }
}

#ifdef HAVE_QR
static uint8_t qrBits[177 * 177 / 8 + 1]; static int qrN = 0; static String qrFor;
static void qrCb(esp_qrcode_handle_t q) {
  qrN = esp_qrcode_get_size(q); memset(qrBits, 0, sizeof qrBits);
  for (int y = 0; y < qrN; y++) for (int x = 0; x < qrN; x++) if (esp_qrcode_get_module(q, x, y)) { int i = y * qrN + x; qrBits[i >> 3] |= 1 << (i & 7); }
}
#endif
static String wifiEsc(const String& s) { String o; for (char c : s) { if (strchr("\\;,:\"", c)) o += '\\'; o += c; } return o; }

static void drawQR() {
  PhoneSetup ph; { Guard g; ph = gPhone; }
  int y = BY + 32;
  String head = qrPurpose == 'w' ? "Type the password on your phone" : "Set up Whisper on your phone";
  for (auto& l : wrapText(head, F_SERIF_L, W - 72)) { drawText(36, y, l, F_SERIF_L); y += textAsc(F_SERIF_L) + 12; }
  y += 14;
  String payload = qrPurpose == 'w' ? "WIFI:T:WPA;S:" + wifiEsc(gSet.hsName) + ";P:" + wifiEsc(gSet.hsPw) + ";;" : "http://" + netIp() + "/";
  int box = 0;
#ifdef HAVE_QR
  if (qrFor != payload) {
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qrCb; cfg.max_qrcode_version = 10; cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    esp_qrcode_generate(&cfg, payload.c_str()); qrFor = payload;
  }
  if (qrN) {
    int cell = max(4, min(9, 240 / qrN)); box = qrN * cell + 28 + 6; int bx = (W - box) / 2;
    boxR(bx, y, box, box, 3);
    for (int r = 0; r < qrN; r++) for (int c = 0; c < qrN; c++) { int i = r * qrN + c; if (qrBits[i >> 3] & (1 << (i & 7))) fillR(bx + 17 + c * cell, y + 17 + r * cell, cell, cell); }
  }
#endif
  y += box + 22;
  String steps[2];
  if (qrPurpose == 'w') { steps[0] = "Scan this code with your phone camera. It joins " + gSet.hsName + "."; steps[1] = "Type the password for " + ph.ssid + " on the page that opens."; }
  else { steps[0] = "Scan this code. Your phone must be on " + (netOnline() ? netSsid() : String("the same Wi-Fi")) + "."; steps[1] = qrPurpose == 'M' ? "Type the name of the model to use on the page that opens." : qrPurpose == 'K' ? "Paste the API key for your Whisper server on the page that opens." : gSet.whSvc == 0 ? "Paste your OpenAI API key on the page that opens." : gSet.whSvc == 2 ? "Paste your Mistral API key on the page that opens." : "Type your Whisper server address on the page that opens."; }
  for (int i = 0; i < 2; i++) {
    drawText(36, y + 4, String(i + 1), F_MONO_M);
    for (auto& l : wrapText(steps[i], F_SERIF_S, W - 72 - 32)) { drawText(68, y, l, F_SERIF_S); y += textAsc(F_SERIF_S) + 12; }
    y += 10;
  }
  int by = BY + BH - 40 - 44;
  drawText(36, by - textAsc(F_MONO_S) - 12, qrPurpose == 'w' ? String("OR OPEN HTTP://192.168.4.1") : "OR OPEN " + upper(deviceUrl()), F_MONO_S, 2);
  fillR(36, by, W - 72, 1);
  if (ph.phase >= 2) fillR(36, by + 15, 14, 14); else boxR(36, by + 15, 14, 14, 2);
  String st = ph.phase == 0 ? String("WAITING FOR YOUR PHONE") : ph.phase == 1 ? String("PHONE CONNECTED") + DOT + "WAITING" : String(qrPurpose == 'w' ? "PASSWORD RECEIVED" : qrPurpose == 'M' ? "MODEL RECEIVED" : (qrPurpose == 'K' || gSet.whSvc != 1) ? "API KEY RECEIVED" : "ADDRESS RECEIVED");
  drawText(62, mid(by, 44, F_MONO_S), st, F_MONO_S, 2);
}

static void drawWave(const uint8_t* lv, int n, int x, int y, int h) {
  for (int i = 0; i < n; i++) { int bh = max(4, lv[48 - n + i] * (h - 4) / 100); fillR(x + i * 11, y + (h - bh) / 2, 7, bh); }
}

static void drawRecord() {
  RecLive l = recLive();
  int y = BY + 36;
  discR(44, y + 8, 8, l.active && !l.paused);
  drawText(64, y, l.paused ? "PAUSED" : "RECORDING", F_MONO_M, 2);
  drawText(36, y + 36, l.title, F_SERIF_M);
  int ty = BY + 250;
  drawText(36, ty, fmtDur(l.t), F_MONO_XXL, -2);
  String mk = l.marks.size() ? String(l.marks.size()) + (l.marks.size() > 1 ? " MARKS" + String(DOT) + "LAST " : " MARK" + String(DOT)) + fmtDur(l.marks.back()) : "NO MARKS";
  drawText(36, ty + textAsc(F_MONO_XXL) + 22, mk, F_MONO_M, 2);
  drawWave(l.lv, 37, 36, BY + BH - 48 - 120, 120);
}

static void drawLibrary() {
  int n = gRecs.size();
  if (!n) { drawText(72, BY + 120, "No recordings yet", F_SERIF_L); drawText(72, BY + 180, "START ONE FROM HOME", F_MONO_S, 2); return; }
  int y = BY + 12;
  for (int i = libTop; i < min(n, libTop + 7); i++) {
    Recording& r = gRecs[i];
    if (i == libSel) marker(22, y + 44);
    fillR(72, y + 86, W - 30 - 72, 2);
    int ta = textAsc(F_SERIF_M), ma = textAsc(F_MONO_S), ty = y + (88 - (ta + 12 + ma)) / 2;
    drawText(72, ty, r.title, F_SERIF_M, 0, W - 30 - 72);
    int my = ty + ta + 12;
    drawText(72, my, fmtStamp(r.ts) + DOT + fmtDur(r.dur), F_MONO_S, 1);
    const char* tag = r.status == REC_DONE ? "TEXT" : r.status == REC_SENDING ? "SENDING" : "AUDIO";
    int tw = textW(tag, F_MONO_S, 2), bw = tw + 12, bh = ma + 8, bx = W - 30 - bw, by = my - 4;
    if (r.status == REC_DONE) { fillR(bx, by, bw, bh); drawText(bx + 6, by + 4, tag, F_MONO_S, 2, 0, 0); }
    else { if (r.status == REC_SENDING) dashR(bx, by, bw, bh); else boxR(bx, by, bw, bh, 2); drawText(bx + 6, by + 4, tag, F_MONO_S, 2); }
    y += 88;
  }
  if (n > 7) drawText(72, y + 14, String(libTop + 1) + "\xE2\x80\x93" + String(min(libTop + 7, n)) + " OF " + String(n), F_MONO_S, 2);
}

static void drawDetail() {
  Recording* r = cur(); if (!r) return;
  int y = BY + 40;
  for (auto& l : wrapText(r->title, F_SERIF_L, 378)) { drawText(72, y, l, F_SERIF_L); y += textAsc(F_SERIF_L) + 14; }
  y += 14;
  String lines[3] = {fmtStamp(r->ts),
                     fmtDur(r->dur) + DOT + mb(r->dur) + (r->marks.size() ? String(DOT) + String(r->marks.size()) + (r->marks.size() > 1 ? " MARKS" : " MARK") : String()),
                     r->status == REC_DONE ? String("TRANSCRIBED") + DOT + "WHISPER" : r->status == REC_SENDING ? String("SENDING TO WHISPER\xE2\x80\xA6") : String("AUDIO ONLY") + DOT + "NOT TRANSCRIBED"};
  for (auto& l : lines) { drawText(72, y, l, F_MONO_S, 2); y += textAsc(F_MONO_S) + 14; }
  const char* acts[3] = {"Listen", r->status == REC_DONE ? "Read transcript" : r->status == REC_SENDING ? "View progress" : "Send to Whisper", "Delete"};
  int y0 = BY + BH - 44 - 3 * 76;
  for (int i = 0; i < 3; i++) { if (detailSel == i) marker(22, y0 + i * 76 + 38); drawText(72, mid(y0 + i * 76, 76, F_SERIF_L), acts[i], F_SERIF_L); }
}

static void drawPlayer() {
  Recording* r = cur(); if (!r) return;
  PlayLive p = playLive();
  int y = BY + 40;
  drawText(36, y, p.playing ? "\xE2\x96\xB6 PLAYING" : (p.pos >= p.dur ? "END" : "PAUSED"), F_MONO_M, 2);
  y += 36;
  for (auto& l : wrapText(r->title, F_SERIF_M, W - 72)) { drawText(36, y, l, F_SERIF_M); y += textAsc(F_SERIF_M) + 12; }
  drawText(36, y + 4, String("OUTPUT") + DOT + "BUILT-IN SPEAKER", F_MONO_S, 2);
  int ty = BY + 260;
  drawText(36, ty, fmtDur(p.pos), F_MONO_XXL, -2);
  drawText(36, ty + textAsc(F_MONO_XXL) + 22, "OF " + fmtDur(p.dur), F_MONO_M, 2);
  int by = BY + BH - 56 - 22, bw = W - 72;
  for (auto m : r->marks) if (r->dur) fillR(36 + (int)((uint64_t)m * bw / r->dur), by - 16, 3, 10);
  boxR(36, by, bw, 22, 2);
  if (p.dur) fillR(41, by + 5, (int)((uint64_t)(bw - 10) * p.pos / p.dur), 12);
}

static void drawSteps(const String& head, const String& sub, const std::vector<String>& labels, const Job& j, bool bar) {
  int y = BY + 40;
  for (auto& l : wrapText(head, F_SERIF_L, 378)) { drawText(72, y, l, F_SERIF_L); y += textAsc(F_SERIF_L) + 12; }
  y += 6;
  for (auto& l : wrapText(sub, F_MONO_S, 378, 1)) { drawText(72, y, l, F_MONO_S, 1); y += textAsc(F_MONO_S) + 9; }
  bool failedRun = j.err.length() > 0;
  int y0 = BY + BH - 44 - (int)labels.size() * 96;
  if (failedRun) {   // full server message, wrapped, between the heading and the steps
    y += 14; int n = 0;
    for (auto& l : wrapText(j.err, F_MONO_S, 378, 1)) { if (y + textAsc(F_MONO_S) > y0 - 8 || ++n > 7) break; drawText(72, y, l, F_MONO_S, 1); y += textAsc(F_MONO_S) + 9; }
  }
  for (size_t i = 0; i < labels.size(); i++) {
    int ry = y0 + i * 96;
    bool failed = failedRun && (int)i == j.step, done = j.done || j.step > (int)i, active = !done && !failed && (int)i == j.step && j.running;
    if (done) fillR(31, ry + 9, 18, 18);
    else if (active) boxR(31, ry + 9, 18, 18, 3);
    else if (failed) { drawText(30, ry + 4, "\xC3\x97", F_SERIF_L); }
    else fillR(37, ry + 15, 6, 6);
    drawText(72, ry + (36 - textAsc(F_SERIF_M)) / 2, labels[i], F_SERIF_M);
    int dy = ry + 44, dx = 72;
    String det = failed ? j.err : (done || active) ? j.det[i] : "";
    if (active && bar && i == 1) { boxR(72, dy, 200, 16, 2); fillR(76, dy + 4, 192 * j.pct / 100, 8); dx += 212; det = String(j.pct) + "%" + (j.det[1].length() ? String(DOT) + j.det[1] : String()); }
    else if (active && !det.length()) det = String("WORKING") + String("...").substring(0, 1 + (millis() / 800) % 3);
    drawText(dx, dy + 1, det, F_MONO_S, 2, W - 30 - dx);
  }
}

static void drawSend() {
  Job j; { Guard g; j = gSend; }
  Recording* r = recFind(j.id);
  String head = j.done ? "Transcript ready" : j.err.length() ? "Could not send" : "Sending to Whisper";
  drawSteps(head, r ? upper(r->title) + DOT + fmtDur(r->dur) : String(), {"Connect to Wi-Fi", "Upload audio", "Transcribe", "Summarise"}, j, true);
}

static void drawProg() {
  Job j; { Guard g; j = gProg; }
  if (j.kind == 't') {
    String sub = String(whSvcName()) + DOT + upper(whModelName());
    drawSteps(j.done ? "Whisper is ready" : j.err.length() ? "Test failed" : "Testing Whisper", sub, {"Reach server", gSet.whSvc != 1 ? "Check API key" : "Check server", "Test transcription"}, j, false);
  } else drawSteps(j.done ? "Connected" : j.err.length() ? "Could not connect" : "Connecting", upper(j.ssid), {"Join network", "Get IP address", "Check internet"}, j, false);
}

static void drawReader() {
  Recording* r = cur(); if (!r) return;
  uint32_t seq; { Guard g; seq = gRepSeq; }
  if (readerFor != curId || readerBuilt != readerMode || readerSeq != seq) { buildPages(curId, readerMode); readerFor = curId; readerBuilt = readerMode; readerSeq = seq; }
  readerPage = min(readerPage, (int)pages.size() - 1);
  int y = BY + 22;
  String pl = String(readerPage + 1) + " / " + String(pages.size());
  int pw = textW(pl, F_MONO_S, 2);
  drawText(W - 36 - pw, y, pl, F_MONO_S, 2);
  drawText(36, y, upper(r->title), F_MONO_S, 2, W - 72 - pw - 16);
  fillR(36, y + textAsc(F_MONO_S) + 14, W - 72, 1);
  y += textAsc(F_MONO_S) + 32;
  int lineH = max(textLineH(F_SERIF_S), textAsc(F_SERIF_S) * 16 / 10);
  for (auto& it : pages[readerPage]) {
    if (it.label.length()) { drawText(36, y, it.label, F_MONO_S, 2); y += textAsc(F_MONO_S) + 9; }
    for (auto& l : it.lines) { drawText(36, y + (lineH - textAsc(F_SERIF_S)) / 2, l, F_SERIF_S); y += lineH; }
    y += 16;
  }
}

static void drawToastAndDialog() {
  if (dlg.on) {
    int x = 34, y = 230, w = 412, h = 300;
    fillR(x + 8, y + 8, w, h); fillR(x, y, w, h, 0); boxR(x, y, w, h, 3);
    drawText(x + 60, y + 28, dlg.title, F_SERIF_L, 0, w - 80);
    drawText(x + 60, y + 28 + textAsc(F_SERIF_L) + 14, dlg.sub, F_MONO_S, 1, w - 80);
    const char* items[2] = {"Cancel", dlg.yes.c_str()};
    for (int i = 0; i < 2; i++) { int ry = y + h - 22 - (2 - i) * 54; if (dlg.sel == i) fillR(x + 23, ry + 19, 16, 16); drawText(x + 60, mid(ry, 54, F_SERIF_L), items[i], F_SERIF_L); }
  }
  String t; { Guard g; if (millis() < toastUntil) t = toastMsg; }
  if (t.length()) {
    int tw = textW(t, F_MONO_S, 2), bw = min(W - 44, tw + 32), bh = textAsc(F_MONO_S) + 20, bx = (W - bw) / 2, by = 800 - 64 - 40 + (40 - bh) / 2;
    fillR(bx, by, bw, bh); drawText(bx + 16, by + 10, t, F_MONO_S, 2, bw - 32, 0);
  }
}

static std::vector<String> hints() {
  if (dlg.on) return {"\xE2\x96\xB2\xE2\x96\xBC SELECT", "PUSH OK", "HOLD CANCEL"};
  if (isMenu(scr)) return {"\xE2\x96\xB2\xE2\x96\xBC SELECT", "PUSH OK", "HOLD BACK"};
  switch (scr) {
    case S_HOME: return {"\xE2\x96\xB2\xE2\x96\xBC SELECT", "PUSH OPEN"};
    case S_KEYBOARD: return {"\xE2\x96\xB2\xE2\x96\xBC CHOOSE", "PUSH ADD", "HOLD CANCEL"};
    case S_QR: return qrPurpose == 'w' ? std::vector<String>{"PUSH TYPE HERE INSTEAD", "HOLD CANCEL"} : std::vector<String>{"HOLD CANCEL"};
    case S_PROG: { Guard g; if (gProg.done) return {"PUSH DONE"}; if (!gProg.running) return {gProg.kind == 't' ? "PUSH TRY AGAIN" : "PUSH BACK", "HOLD BACK"}; return {"HOLD CANCEL"}; }
    case S_RECORD: return {recLive().paused ? "PUSH RESUME" : "PUSH PAUSE", "\xE2\x96\xB2\xE2\x96\xBC MARK", "HOLD SAVE"};
    case S_LIBRARY: return {"\xE2\x96\xB2\xE2\x96\xBC SCROLL", "PUSH OPEN", "HOLD BACK"};
    case S_DETAIL: return {"\xE2\x96\xB2\xE2\x96\xBC SELECT", "PUSH OK", "HOLD BACK"};
    case S_PLAYER: return {playLive().playing ? "PUSH PAUSE" : "PUSH PLAY", "\xE2\x96\xB2\xE2\x96\xBC 10 SEC", "HOLD BACK"};
    case S_SEND: { Guard g; if (gSend.done) return {"PUSH READ", "HOLD BACK"}; if (gSend.err.length()) return {"PUSH RETRY", "HOLD BACK"}; return {"HOLD CONTINUE IN BACKGROUND"}; }
    case S_READER: { int nx = (readerMode + 1) % max(2, readerModes); String nn = nx < (int)readerNames.size() ? readerNames[nx] : String("SUMMARY"); if (nn.length() > 12) nn = nn.substring(0, 11) + "\xE2\x80\xA6"; return {"\xE2\x96\xB2\xE2\x96\xBC PAGE", "PUSH " + nn, "HOLD BACK"}; }
    default: return {};
  }
}

// full screen while a downloaded update is written: the one moment the device must not be switched off
static void drawUpdating() {
  int pct; String ver; { Guard g; pct = gUpd.pct / 5 * 5; ver = gUpd.latest; }   // 5 % steps: fewer e-paper refreshes
  int y = BY + 60;
  drawText(36, y, "Updating", F_SERIF_XL);
  y += textAsc(F_SERIF_XL) + 24;
  drawText(36, y, "TO VERSION " + ver + String(DOT) + "FROM GITHUB", F_MONO_S, 2, W - 72);
  y += textAsc(F_MONO_S) + 60;
  String p = String(pct) + "%";
  drawText(36, y, p, F_MONO_XXL, -2);
  y += textAsc(F_MONO_XXL) + 30;
  boxR(36, y, W - 72, 28, 2);
  fillR(40, y + 4, (W - 80) * pct / 100, 20);
  y += 28 + 60;
  fillR(0, y, W, 90);
  drawText((W - textW("DO NOT SWITCH OFF", F_MONO_S, 2)) / 2, mid(y, 90, F_MONO_S), "DO NOT SWITCH OFF", F_MONO_S, 2, 0, 0);
  y += 90 + 30;
  drawText(36, y, "RESTARTS BY ITSELF WHEN DONE", F_MONO_S, 2, W - 72);
}

static void render() {
  dispClear();
  drawHeader();
  bool upd; { Guard g; upd = gUpd.installing; }
  if (upd) { drawUpdating(); drawToastAndDialog(); bool ch = drawnScr != S_COUNT; drawnScr = S_COUNT; dispCommit(ch); return; }
  switch (scr) {
    case S_HOME: drawHome(); break;
    case S_KEYBOARD: drawKeyboard(); break;
    case S_QR: drawQR(); break;
    case S_PROG: drawProg(); break;
    case S_RECORD: drawRecord(); break;
    case S_LIBRARY: drawLibrary(); break;
    case S_DETAIL: drawDetail(); break;
    case S_PLAYER: drawPlayer(); break;
    case S_SEND: drawSend(); break;
    case S_READER: drawReader(); break;
    default: if (isMenu(scr)) drawMenu(); break;
  }
  drawHints(hints());
  drawToastAndDialog();
  bool changed = scr != drawnScr; drawnScr = scr;
  dispCommit(changed);
}

// react to things that happened outside the rocker (web UI, background jobs)
static void observe() {
  RecLive l = recLive();
  if (l.active && !wasRecActive && scr != S_RECORD) { dlg.on = false; scr = S_RECORD; }
  if (!l.active && wasRecActive && scr == S_RECORD) { curId = l.id; detailSel = 1; scr = recFind(l.id) ? S_DETAIL : S_HOME; }
  wasRecActive = l.active;
  if ((scr == S_DETAIL || scr == S_PLAYER || scr == S_READER) && !cur()) { dlg.on = false; scr = S_LIBRARY; libSel = min(libSel, max(0, (int)gRecs.size() - 1)); libTop = min(libTop, libSel); }
  if (libSel >= (int)gRecs.size()) { libSel = max(0, (int)gRecs.size() - 1); libTop = min(libTop, libSel); }
  String t; { Guard g; if (gProg.toast.length() && !gProg.running) { t = gProg.toast; gProg.toast = ""; } }
  if (t.length()) uiToast(t);
  PhoneSetup ph; { Guard g; ph = gPhone; }
  if (ph.active && ph.phase == 2 && scr == S_QR) {
    endQR();
    if (ph.purpose == 'w') startJoin(ph.ssid, ph.value, ph.sec, S_WIFIMETHOD);
    else { { Guard g; if (ph.purpose == 'K') gSet.whSrvKey = ph.value; else if (ph.purpose == 'M') whModelTyped() = ph.value; else if (gSet.whSvc != 1) whApiKey() = ph.value; else gSet.whServer = ph.value; gSet.whOk = false; } settingsSave(); startTest(); }
  }
}

// Shown before deep sleep ('S'), power-off with PWR ('O') or on an empty battery ('B'). The e-paper keeps it
// without power, so it must say plainly that the device is not running and how to get it back.
void uiSleepScreen(char why) {
  dispClear();
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  char clk[8]; snprintf(clk, sizeof clk, "%02d:%02d", tm.tm_hour, tm.tm_min);
  int pct = batteryPercent();
  bool b = why == 'B', o = why == 'O';
  drawText(22, mid(0, 46, F_MONO_S), b ? "BATTERY EMPTY" : o ? "SWITCHED OFF" : "ASLEEP", F_MONO_S, 2);
  fillR(0, 46, W, 2);
  // big inverted band: readable from across the room
  const char* word = b ? "EMPTY" : o ? "OFF" : "ASLEEP";
  int bandY = BY + 32, bandH = 168;
  fillR(0, bandY, W, bandH);
  drawText((W - textW(word, F_MONO_XXL)) / 2, mid(bandY, bandH, F_MONO_XXL), word, F_MONO_XXL, 0, 0, 0);
  int y = bandY + bandH + 36;
  drawText(72, y, b ? "Battery empty" : o ? "Switched off" : "Asleep", F_SERIF_XL);
  y += textAsc(F_SERIF_XL) + 28;
  String l1 = b ? "CONNECT USB-C TO CHARGE" : String("SINCE ") + (clockValid() ? String(clk) : String("--:--")) + (pct >= 0 ? String(DOT) + "BATTERY " + pct + "%" : String());
  drawText(72, y, l1, F_MONO_S, 2, W - 72 - 32);
  y += textAsc(F_MONO_S) + 14;
  drawText(72, y, b ? "THE RECORDING WAS SAVED" : "NOT RECORDING" + String(DOT) + "WI-FI OFF", F_MONO_S, 2, W - 72 - 32);
  y += textAsc(F_MONO_S) + 14;
  if (!b && !o) drawText(72, y, gSet.sleepMin ? "AFTER " + String(gSet.sleepMin) + " MIN WITHOUT USE" : String("TO SAVE THE BATTERY"), F_MONO_S, 2, W - 72 - 32);
  int la = textAsc(F_SERIF_XL), ty = BY + BH - 44 - 136 + (136 - (la + 16 + textAsc(F_MONO_S))) / 2;
  marker(22, ty + la / 2);
  drawText(72, ty, b ? "Charge" : o ? "Press PWR" : "Push", F_SERIF_XL);
  drawText(72, ty + la + 16, b ? "IT STARTS BY ITSELF ON THE CHARGER" : o ? "TO SWITCH IT ON AGAIN" : "ANY DIRECTION OR A CHARGER WAKES IT", F_MONO_S, 2, W - 72 - 32);
  drawHints({b ? "CHARGE FIRST" : o ? "PWR SWITCHES ON" : "PUSH TO WAKE"});
  dispRequestFull(b ? "EMPTY" : o ? "OFF" : "SLEEP");
  dispCommit(true);
  dispHibernate();
}

void uiBegin() {
  keyQ = xQueueCreate(16, sizeof(Key));
  xTaskCreatePinnedToCore(inputTask, "input", 3072, nullptr, 4, nullptr, 1);
  render();
}

void uiLoop() {
  Key k;
  if (xQueueReceive(keyQ, &k, pdMS_TO_TICKS(400)) == pdTRUE) {
    do { if (k != K_POKE) handleKey(k); } while (xQueueReceive(keyQ, &k, 0) == pdTRUE);
  }
  observe();
  powerLoop();
  render();   // cheap: the panel only refreshes the bands that differ from the last frame
}
