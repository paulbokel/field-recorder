#include "app.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_mac.h>

Settings gSet;
static Preferences P;

String deviceName() {   // unique per board: field-xxxx from the Wi-Fi MAC
  static String n;
  if (!n.length()) { uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA); char b[16]; snprintf(b, sizeof b, MDNS_NAME "-%02x%02x", mac[4], mac[5]); n = b; }
  return n;
}
String randomPw() {
  static const char* W[] = {"kestrel", "harbor", "linden", "quartz", "meadow", "cobalt", "fennel", "orbit"};
  return String(W[esp_random() % 8]) + "-" + String(10 + esp_random() % 90);
}
String whModelName() {
  if (gSet.whSvc == 0) return gSet.whOaModel.length() ? gSet.whOaModel : String(OPENAI_STT_MODEL);
  if (gSet.whSvc == 2) return gSet.whMiModel.length() ? gSet.whMiModel : String(MISTRAL_STT_MODEL);
  return gSet.whSrvModel.length() ? gSet.whSrvModel : String(SELF_MODELS[gSet.whModel % 3]);
}
String& whModelTyped() { return gSet.whSvc == 0 ? gSet.whOaModel : gSet.whSvc == 2 ? gSet.whMiModel : gSet.whSrvModel; }
String& whApiKey() { return gSet.whSvc == 0 ? gSet.whKey : gSet.whSvc == 2 ? gSet.whMiKey : gSet.whSrvKey; }
bool whHasCred() { return gSet.whSvc == 1 ? gSet.whServer.length() > 0 : whApiKey().length() > 0; }
bool whDiarize() { return gSet.whSvc == 2 && gSet.whSpk; }
const char* whSvcName() { return gSet.whSvc == 0 ? "OPENAI" : gSet.whSvc == 2 ? "MISTRAL" : "SELF-HOSTED"; }
String deviceUrl() { return "http://" + deviceName() + ".local"; }
String maskKey(const String& k) {
  if (k.length() < 10) return k.length() ? "SET" : "";
  return k.substring(0, 8) + "\xE2\x80\xA6" + k.substring(k.length() - 4);
}

void settingsLoad() {
  P.begin("field", false);
  Guard g;
  gSet.wifiOn = P.getBool("wifiOn", true);
  JsonDocument d;
  if (!deserializeJson(d, P.getString("nets", "[]")))
    for (JsonObject o : d.as<JsonArray>()) gSet.nets.push_back({o["s"].as<String>(), o["p"].as<String>(), String(o["e"] | "WPA2")});
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char def[16]; snprintf(def, sizeof def, "FIELD-%02X%02X", mac[4], mac[5]);
  gSet.hsOn = P.getBool("hsOn", false);
  gSet.hsName = P.getString("hsName", def);
  gSet.hsPw = P.getString("hsPw", "");
  gSet.whSvc = P.getUChar("whSvc", 0);
  gSet.whKey = P.getString("whKey", "");
  gSet.whServer = P.getString("whServer", "");
  gSet.whSrvKey = P.getString("whSrvKey", "");
  gSet.whOaModel = P.getString("whOaModel", "");
  gSet.whSrvModel = P.getString("whSrvModel", "");
  gSet.whMiKey = P.getString("whMiKey", ""); gSet.whMiModel = P.getString("whMiModel", ""); gSet.whSpk = P.getBool("whSpk", true);
  gSet.aiUrl = P.getString("aiUrl", ""); gSet.aiKey = P.getString("aiKey", ""); gSet.aiModel = P.getString("aiModel", "");
  gSet.whModel = P.getUChar("whModel", 0);
  gSet.whLang = P.getUChar("whLang", 0);
  gSet.whOk = P.getBool("whOk", false);
  gSet.autoSend = P.getBool("auto", false);
  gSet.fullEvery = P.getUShort("fullEvery", 30);
  gSet.sleepMin = P.getUShort("sleepMin", SLEEP_MIN_DEFAULT);
  gSet.volume = P.getUChar("vol2", 100);
  gSet.nextId = P.getULong("nextId", 1);
  if (gSet.hsPw.length() < 8) { gSet.hsPw = randomPw(); P.putString("hsPw", gSet.hsPw); }
}

void settingsSave() {
  Guard g;
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  for (auto& n : gSet.nets) { JsonObject o = a.add<JsonObject>(); o["s"] = n.ssid; o["p"] = n.pw; o["e"] = n.sec; }
  String s; serializeJson(d, s);
  P.putBool("wifiOn", gSet.wifiOn); P.putString("nets", s);
  P.putBool("hsOn", gSet.hsOn); P.putString("hsName", gSet.hsName); P.putString("hsPw", gSet.hsPw);
  P.putUChar("whSvc", gSet.whSvc); P.putString("whKey", gSet.whKey); P.putString("whServer", gSet.whServer); P.putString("whSrvKey", gSet.whSrvKey); P.putString("whOaModel", gSet.whOaModel); P.putString("whSrvModel", gSet.whSrvModel);
  P.putString("whMiKey", gSet.whMiKey); P.putString("whMiModel", gSet.whMiModel); P.putBool("whSpk", gSet.whSpk);
  P.putString("aiUrl", gSet.aiUrl); P.putString("aiKey", gSet.aiKey); P.putString("aiModel", gSet.aiModel);
  P.putUChar("whModel", gSet.whModel); P.putUChar("whLang", gSet.whLang); P.putBool("whOk", gSet.whOk);
  P.putBool("auto", gSet.autoSend); P.putUShort("fullEvery", gSet.fullEvery); P.putUShort("sleepMin", gSet.sleepMin); P.putUChar("vol2", gSet.volume);
  P.putULong("nextId", gSet.nextId);
}
