#include "app.h"
#include <FFat.h>
#include <ArduinoJson.h>

// Report types ("Meeting minutes", "Meeting report", "SOEP-verslag" + user-defined).
// Built-in prompts live in firmware; edits and user types are stored in /cfg/reports.json on the
// internal flash, so they survive firmware updates and stay with the device when the SD card changes.

static const char* const B_ID[] = {"minutes", "report", "soep"};
static const char* const B_NAME[] = {"Meeting minutes", "Meeting report", "SOEP-verslag"};

static const char P_MINUTES[] = R"P(You write formal meeting minutes from a transcript of a recorded meeting.

INPUT
You get the recording title, date and duration, followed by the transcript. Each paragraph starts with a timestamp [mm:ss]. The transcript does not say who is speaking; only use names that are said out loud.

OUTPUT
Write the minutes in the same language as the transcript, in Markdown, with exactly these sections:

# Minutes: <subject of the meeting>
**Date:** <date> · **Duration:** <duration>
**Present:** <names mentioned as taking part, or "Not recorded">

## Agenda items
For every topic discussed, in the order it came up:
### <number>. <topic>
- What was discussed: 1 to 4 short, factual sentences.
- **Decision:** what was decided (leave this line out if nothing was decided).

## Decisions
A numbered list of every decision, each as one clear sentence.

## Action items
| Who | What | Deadline |
|---|---|---|
One row per task. Use "Not assigned" or "Not set" when the transcript does not say.

## Open points
Questions or topics that were left open or postponed. Write "None" when there are none.

## Next meeting
Date, time or topics for a follow-up, or "Not discussed".

RULES
- Be factual, neutral and concise. Write in the third person and the past tense ("The team agreed...").
- Never invent names, numbers, dates or decisions. If something is unclear in the transcript, write [unclear].
- Leave out small talk, repetitions and filler words.
- Keep amounts, dates and names exactly as they were said.)P";

static const char P_REPORT[] = R"P(You write a clear meeting report from a transcript of a recorded meeting, for readers who were not there.

INPUT
You get the recording title, date and duration, followed by the transcript. Each paragraph starts with a timestamp [mm:ss]. The transcript does not say who is speaking; only use names that are said out loud.

OUTPUT
Write the report in the same language as the transcript, in Markdown, with these sections:

# <Descriptive title of the meeting>
*<date> · <duration>*

## Summary
3 to 5 sentences: why the meeting took place, what the most important outcomes are and what happens next.

## Background
The reason for the meeting and its context, as far as the transcript makes it clear. Leave this section out if there is nothing to say.

## Discussion
Group what was said into themes (not in strict time order). Give every theme a ### heading and describe it in short paragraphs of running prose: the points raised, the arguments for and against, and where people agreed or disagreed.

## Conclusions and decisions
A bullet list of what was concluded or decided.

## Follow-up
A bullet list of next steps, each with who and when if that was said.

## Points of attention
Risks, open questions or worries that were raised. Leave this section out if there are none.

RULES
- Write for someone who was not there: explain abbreviations and context the first time they appear, if the transcript makes them clear.
- Be accurate. Never add facts, names or numbers that are not in the transcript; write [unclear] where the transcript is unclear.
- Use plain, active language and short paragraphs. Leave out small talk and repetitions.)P";

static const char P_SOEP[] = R"P(Je bent een zorgmedewerker in een woonzorgcentrum of verpleeghuis en schrijft een rapportage volgens de SOEP-methode. De bron is een transcript van een gesproken overdracht, gesprek of observatie.

INVOER
Je krijgt de titel, datum en duur van de opname en daarna het transcript. Elke alinea begint met een tijdsaanduiding [mm:ss]. Het transcript zegt niet wie er spreekt.

UITVOER
Schrijf in het Nederlands, in Markdown, op taalniveau B1. Gaat het over meer dan één cliënt, maak dan per cliënt een aparte rapportage. Gebruik per cliënt precies deze opbouw:

# SOEP-rapportage: <naam of omschrijving van de cliënt zoals genoemd, anders "cliënt">
**Datum:** <datum> · **Bron:** gesproken opname (<duur>)

## S – Subjectief
Wat de cliënt (of familie of naaste) zelf zegt, ervaart of aangeeft: klachten, pijn, stemming, wensen en zorgen. Gebruik de woorden van de cliënt waar dat helpt, tussen aanhalingstekens. Schrijf bijvoorbeeld: "Mevrouw geeft aan dat ...".

## O – Objectief
Wat de zorgmedewerker heeft waargenomen of gemeten: gedrag, uiterlijk, mobiliteit, eten en drinken, uitscheiding, huid, slaap, medicatie-inname, gegeven zorg en meetwaarden (bijvoorbeeld bloeddruk, temperatuur, gewicht of pijnscore). Alleen feiten, geen interpretatie.

## E – Evaluatie
De beoordeling van de zorgmedewerker: wat betekenen S en O samen, hoe verloopt het ten opzichte van het zorgplan of de doelen, is er verbetering, achteruitgang of een risico (bijvoorbeeld vallen, decubitus, ondervoeding, onrust)? Schrijf alleen conclusies die in het transcript uitgesproken worden of er direct uit volgen. Stel geen medische diagnoses.

## P – Plan
De afspraken en vervolgacties: wat wordt er gedaan, door wie en wanneer; wat wordt geobserveerd; wie wordt geïnformeerd (arts, familie, EVV'er, collega's). Gebruik een opsommingslijst.

REGELS
- Schrijf feitelijk, respectvol en zakelijk, in de derde persoon ("Meneer", "Mevrouw" of de genoemde naam). Geen oordelen of etiketten.
- Verzin niets. Is er voor een onderdeel niets besproken, schrijf dan "Niet besproken." Zet onduidelijke delen tussen [onduidelijk].
- Neem meetwaarden, tijden, medicijnnamen en doseringen precies over zoals ze genoemd worden.
- Laat informatie weg die niets met de zorg te maken heeft.
- Houd zinnen kort.)P";

static const char* const B_PROMPT[] = {P_MINUTES, P_REPORT, P_SOEP};
static const int NB = 3;

static std::vector<ReportType> gTypes;
static String gAuto;   // type id generated after every transcription, "" = none
static const char* CFG = "/cfg/reports.json";

static void save() {
  if (!gCfgFS) return;
  JsonDocument d;
  JsonObject over = d["over"].to<JsonObject>();
  JsonArray cus = d["custom"].to<JsonArray>();
  for (auto& t : gTypes) {
    if (t.builtin) { if (t.edited) over[t.id] = t.prompt; }
    else { JsonObject o = cus.add<JsonObject>(); o["id"] = t.id; o["name"] = t.name; o["prompt"] = t.prompt; }
  }
  d["auto"] = gAuto;
  if (!gCfgFS->exists("/cfg")) gCfgFS->mkdir("/cfg");
  File f = gCfgFS->open(CFG, FILE_WRITE); if (!f) { Serial.println("[reports] cannot write config"); return; }
  serializeJson(d, f); f.close();
}

void reportsBegin() {
  Guard g;
  gTypes.clear();
  JsonDocument d;
  if (gCfgFS) { File f = gCfgFS->open(CFG); if (f) { deserializeJson(d, f); f.close(); } }
  for (int i = 0; i < NB; i++) {
    ReportType t; t.id = B_ID[i]; t.name = B_NAME[i]; t.builtin = true; t.prompt = B_PROMPT[i];
    if (d["over"][t.id].is<const char*>()) { t.prompt = d["over"][t.id].as<String>(); t.edited = t.prompt != B_PROMPT[i]; }
    gTypes.push_back(t);
  }
  for (JsonObject o : d["custom"].as<JsonArray>()) {
    ReportType t; t.id = o["id"].as<String>(); t.name = o["name"].as<String>(); t.prompt = o["prompt"].as<String>();
    if (t.id.length()) gTypes.push_back(t);
  }
  gAuto = d["auto"] | "";
  ReportType tmp; if (gAuto.length() && !reportType(gAuto, tmp)) gAuto = "";
  Serial.printf("[reports] %u report types\n", (unsigned)gTypes.size());
}

std::vector<ReportType> reportTypes() { Guard g; return gTypes; }
bool reportType(const String& id, ReportType& out) { Guard g; for (auto& t : gTypes) if (t.id == id) { out = t; return true; } return false; }
String reportDefaultPrompt(const String& id) { for (int i = 0; i < NB; i++) if (id == B_ID[i]) return B_PROMPT[i]; return ""; }
String reportAuto() { Guard g; return gAuto; }
void reportSetAuto(const String& id) { Guard g; ReportType t; gAuto = (id.length() && reportType(id, t)) ? id : ""; save(); }

String reportSave(const String& id, const String& name, const String& prompt) {
  Guard g;
  for (auto& t : gTypes) if (t.id == id && id.length()) {
    if (!t.builtin && name.length()) t.name = name;
    t.prompt = prompt; if (t.builtin) t.edited = prompt != reportDefaultPrompt(id);
    save(); return t.id;
  }
  ReportType t; t.id = "c" + String((uint32_t)esp_random() % 0xFFFFFF, HEX); t.name = name.length() ? name : String("New report type"); t.prompt = prompt;
  gTypes.push_back(t); save(); return t.id;
}
bool reportReset(const String& id) {
  Guard g; for (auto& t : gTypes) if (t.id == id && t.builtin) { t.prompt = reportDefaultPrompt(id); t.edited = false; save(); return true; }
  return false;
}
bool reportDelete(const String& id) {
  Guard g;
  for (size_t i = 0; i < gTypes.size(); i++) if (gTypes[i].id == id && !gTypes[i].builtin) {
    gTypes.erase(gTypes.begin() + i); if (gAuto == id) gAuto = ""; save(); return true;
  }
  return false;
}

// ---------- reports stored per recording: /rec/<id>.rep.json ----------
static String repPath(uint32_t id) { return "/rec/" + String(id) + ".rep.json"; }

std::vector<RecReport> recReports(uint32_t id) {
  std::vector<RecReport> v; if (!gFS) return v;
  File f = gFS->open(repPath(id)); if (!f) return v;
  JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close(); if (e) return v;
  for (JsonObject o : d.as<JsonArray>()) { RecReport r; r.type = o["t"].as<String>(); r.name = o["n"].as<String>(); r.ts = o["ts"] | 0; r.text = o["x"].as<String>(); v.push_back(r); }
  return v;
}
static bool writeReports(uint32_t id, const std::vector<RecReport>& v) {
  if (!gFS) return false;
  if (v.empty()) { gFS->remove(repPath(id)); return true; }
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  for (auto& r : v) { JsonObject o = a.add<JsonObject>(); o["t"] = r.type; o["n"] = r.name; o["ts"] = r.ts; o["x"] = r.text; }
  File f = gFS->open(repPath(id), FILE_WRITE); if (!f) return false;
  serializeJson(d, f); f.close(); return true;
}
bool recReportPut(uint32_t id, const RecReport& r) {
  auto v = recReports(id);
  bool rep = false; for (auto& x : v) if (x.type == r.type) { x = r; rep = true; }
  if (!rep) v.push_back(r);
  return writeReports(id, v);
}
bool recReportDelete(uint32_t id, const String& type) {
  auto v = recReports(id); size_t n = v.size();
  for (size_t i = 0; i < v.size(); i++) if (v[i].type == type) { v.erase(v.begin() + i); break; }
  return v.size() != n && writeReports(id, v);
}
