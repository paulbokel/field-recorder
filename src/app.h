#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include <FS.h>
#include "config.h"

extern SemaphoreHandle_t gLock;
struct Guard { Guard() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); } ~Guard() { xSemaphoreGiveRecursive(gLock); } };

static const char* const SELF_MODELS[] = {"large-v3", "medium", "small"};
static const char* const LANG_CODES[]  = {"", "en", "nl", "de", "fr"};
static const char* const LANG_NAMES[]  = {"AUTO-DETECT", "ENGLISH", "NEDERLANDS", "DEUTSCH", "FRANÇAIS"};

// ---- util (main.cpp)
String fmtDur(uint32_t s);
String fmtStamp(uint32_t ts);
String upper(String s);
bool clockValid();

// ---- board.cpp
void boardBegin();
int  batteryPercent();          // -1 = no battery / unknown
void rtcToSystem();
void systemToRtc();
bool boardVbus();               // USB power present
int  batteryMv();               // 0 = unknown
void boardSleepRails();         // switch off what is not needed in deep sleep
char boardPwrKey();             // PWR button since last call: 'L' held, 'S' tapped, 0 nothing
void boardPowerOff();           // PMIC cuts all power; PWR switches the device back on

// ---- power.cpp
void powerActivity();           // a person used the device (rocker, web page in front)
uint32_t powerIdleMs();
void powerWakeCheck();          // right after boardBegin(): timer wake without a charger sleeps on
void powerBootCheck();
void powerLoop();
[[noreturn]] void powerSleep(char why);   // 'S' idle, 'B' battery empty
[[noreturn]] void powerOff();             // switched off with the PWR button

// ---- ota.cpp
void otaBegin();                // called at the end of setup(): starts the rollback watchdog for a fresh update
void otaLoop();                 // confirms a fresh update once the device has run fine for a while
void otaConfirm();              // confirm now (before sleeping or switching off)
String otaSlot();               // running app partition, e.g. "app1"
bool otaRolledBack();           // the last update failed to start and the previous firmware came back
struct UpdState { bool checking = false, installing = false, avail = false; String latest, notes, err; uint32_t checkedAt = 0; int pct = 0; };
extern UpdState gUpd;           // under Guard
bool otaCheckStart();           // ask GitHub for the latest release (background)
bool otaInstallStart();         // download and install gUpd.latest (background), restarts when done

// ---- settings.cpp
struct SavedNet { String ssid, pw, sec; };
// whSvc: 0 OpenAI, 1 self-hosted Whisper server, 2 Mistral Voxtral
// whSrvKey: optional API key for a self-hosted Whisper server; whMiKey: Mistral API key
// whOaModel / whSrvModel / whMiModel: model typed by the user per service ("" = default)
// whSpk: Mistral labels speakers (the language is then detected, Mistral cannot combine both)
struct Settings {
  String aiUrl, aiKey, aiModel;   // AI for summaries and reports ("" = OpenAI, Whisper key, gpt-4o-mini)
  bool wifiOn = true; std::vector<SavedNet> nets;
  bool hsOn = false; String hsName, hsPw;
  uint8_t whSvc = 0; String whKey, whServer, whSrvKey, whOaModel, whSrvModel, whMiKey, whMiModel; uint8_t whModel = 0, whLang = 0; bool whOk = false, whSpk = true;
  bool autoSend = false; uint16_t fullEvery = 30; uint16_t sleepMin = SLEEP_MIN_DEFAULT; uint8_t volume = 100; uint32_t nextId = 1;
};
extern Settings gSet;
void settingsLoad();
void settingsSave();
String randomPw();
String deviceName();
String whModelName();   // model sent to the current Whisper service
String& whModelTyped(); // the typed model of the current service (under Guard)
String& whApiKey();     // the API key of the current service (under Guard)
bool whHasCred();       // key (OpenAI, Mistral) or server address (self-hosted) is set
bool whDiarize();       // the current service labels speakers
const char* whSvcName();   // "OPENAI", "SELF-HOSTED", "MISTRAL"   // "field-9fbc": mDNS + DHCP hostname, unique per device
String deviceUrl();    // "http://field-9fbc.local"
String maskKey(const String& k);

// ---- storage.cpp
enum RecStatus : uint8_t { REC_LOCAL = 0, REC_SENDING = 1, REC_DONE = 2 };
struct Recording { uint32_t id = 0; String title; uint32_t ts = 0, dur = 0; std::vector<uint32_t> marks; uint8_t status = REC_LOCAL;
                   String rtype; };   // report type for this recording: "" = the default, "none" = no report
// spk: speaker per paragraph (0 = unknown, 1..n), empty when the service does not label speakers.
// names: name given to speaker n at names[n-1] ("" = not named yet).
struct Transcript { std::vector<std::pair<uint32_t, String>> segs; std::vector<String> sum, act; std::vector<uint8_t> spk; std::vector<String> names; };
String speakerName(const Transcript& t, size_t seg);   // "" when that paragraph has no speaker
extern std::vector<Recording> gRecs;   // newest first
extern fs::FS* gFS;
extern fs::FS* gCfgFS;   // internal flash (FFat) for configuration; falls back to gFS
extern bool gSdOk;
bool storageBegin();
void storageLoadIndex();
bool storageSaveMeta(const Recording& r, bool open = false);
bool storageDelete(uint32_t id);
bool storageLoadTranscript(uint32_t id, Transcript& t);
bool storageSaveTranscript(uint32_t id, const Transcript& t);
String recWavPath(uint32_t id);
Recording* recFind(uint32_t id);
uint64_t storageUsed();
uint64_t storageTotal();
void wavHeader(uint8_t* h, uint32_t dataBytes);

// ---- reports.cpp
struct ReportType { String id, name, prompt; bool builtin = false, edited = false; };
struct RecReport { String type, name, text; uint32_t ts = 0; };
void reportsBegin();
std::vector<ReportType> reportTypes();
bool reportType(const String& id, ReportType& out);
String reportDefaultPrompt(const String& id);
String reportSave(const String& id, const String& name, const String& prompt);   // returns id (new id for a new type)
bool reportReset(const String& id);
bool reportDelete(const String& id);
String reportAuto();
void reportSetAuto(const String& id);
std::vector<RecReport> recReports(uint32_t id);
bool recReportPut(uint32_t id, const RecReport& r);
bool recReportDelete(uint32_t id, const String& type);

// ---- display.cpp
enum FontId : uint8_t { F_SERIF_XL, F_SERIF_L, F_SERIF_M, F_SERIF_S, F_MONO_XXL, F_MONO_XL, F_MONO_L, F_MONO_M, F_MONO_S, F_COUNT };
void dispBegin();
void dispClear();
void dispCommit(bool screenChanged);
void dispRequestFull(const char* why);
void dispHibernate();
extern uint32_t gPartials, gFulls; extern uint16_t gSinceFull;
int  textW(const String& s, FontId f, int track = 0);
int  textAsc(FontId f);
int  textLineH(FontId f);
int  drawText(int x, int y, const String& s, FontId f, int track = 0, int maxW = 0, uint16_t c = 1);
std::vector<String> wrapText(const String& s, FontId f, int maxW, int track = 0);
void fillR(int x, int y, int w, int h, uint16_t c = 1);
void boxR(int x, int y, int w, int h, int t = 2, uint16_t c = 1);
void dashR(int x, int y, int w, int h);
void discR(int cx, int cy, int r, bool fill);

// ---- audio.cpp
struct RecLive { bool active = false, paused = false; uint32_t t = 0, id = 0, ts = 0; std::vector<uint32_t> marks; uint8_t lv[48] = {0}; String title; };
struct PlayLive { bool active = false, playing = false; uint32_t id = 0, pos = 0, dur = 0; };
bool audioBegin();
bool recStart();
void recTogglePause();
void recMark();
uint32_t recStop();
RecLive recLive();
bool playStart(uint32_t id, uint32_t pos);
void playToggle();
void playSeek(int delta);
void playSeekTo(uint32_t pos);
void playStop();
PlayLive playLive();
void audioSetVolume(uint8_t v);

// ---- net.cpp
struct ScanNet { String ssid, sec; int rssi = -100; uint8_t sig = 0; };
void netBegin();
void netDnsLoop();
bool netOnline();
String netSsid();
String netIp();
bool netApOn();
bool netApAuto();       // device network is up only because Wi-Fi is offline
String netWebUrl();     // "" while nothing is up yet
int  netApClients();
void netSetWifiOn(bool on);
void netSetHotspot(bool on);
void netSetApTemp(bool on);
void netSetApAllowed(bool on);   // power management: may the fallback network be up?
void netRestartAp();
void netDisconnect();
void netReconnectSoon();
void netScanStart();
bool netScanning();
bool netHaveScan();
std::vector<ScanNet> netScan();
int  netSigFor(const String& ssid);   // -1 unknown, 0 = not in range, 1..4
bool netJoin(const String& ssid, const String& pw, std::function<void(int, const String&)> step, String& err);
bool netCheckInternet();

// ---- jobs.cpp (transcription, Wi-Fi join, Whisper test)
struct Job { bool running = false, done = false; char kind = 0; uint32_t id = 0; int step = 0, pct = 0; String err, det[4], toast, ssid; };
extern Job gSend, gProg, gRep;   // gRep: report generation (ssid = type id, det[0] = type name)
extern uint32_t gRepSeq;          // bumps whenever a report is written or a speaker is named
bool reportStart(uint32_t id, const String& type);
String aiRoot(); String aiModelName(); bool aiReady();   // gProg.kind: 'j' join, 't' Whisper test
bool sendStart(uint32_t id);
bool joinStart(const String& ssid, const String& pw, const String& sec);
bool whisperTestStart();
void onRecordingSaved(uint32_t id);

// ---- phone setup handshake (QR flow)
struct PhoneSetup { bool active = false; char purpose = 'w'; String ssid, sec; int phase = 0; String value; };
extern PhoneSetup gPhone;

// ---- ui.cpp
void uiBegin();
void uiLoop();
void uiPoke();
void uiToast(const String& m);
void uiSleepScreen(char why);

// ---- web.cpp
void webBegin();
