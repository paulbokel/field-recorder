#include "app.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include "fonts.h"

// Portrait 480x800 framebuffer. Each commit diffs it against the last pushed frame and
// partially refreshes only the changed bands. Full refresh every gSet.fullEvery partials,
// taken on the next screen change so it never interrupts a live screen.
static GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>
  epd(GxEPD2_397_GDEM0397T81(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static const int DW = 480, DH = 800, ROWB = DW / 8, BYTES = ROWB * DH;
static GFXcanvas1* cv = nullptr;
static uint8_t* prevBuf = nullptr;
static bool havePrev = false, fullReq = true;
static const char* fullWhy = "BOOT";
uint32_t gPartials = 0, gFulls = 0; uint16_t gSinceFull = 0;

void dispBegin() {
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  epd.epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
  epd.init(0, true, 10, false);
  epd.setRotation(EPD_ROTATION);
  cv = new GFXcanvas1(DW, DH);
  cv->setTextWrap(false);
  prevBuf = (uint8_t*)heap_caps_malloc(BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!prevBuf) prevBuf = (uint8_t*)malloc(BYTES);
  cv->fillScreen(0);
}

void dispClear() { cv->fillScreen(0); }
void dispHibernate() { epd.hibernate(); }
void dispRequestFull(const char* why) { fullReq = true; fullWhy = why; }

static void blit(int x, int y, int w, int h) {
  const uint8_t* b = cv->getBuffer();
  for (int yy = y; yy < y + h; yy++) {
    const uint8_t* row = b + yy * ROWB;
    for (int xx = x; xx < x + w; xx++) epd.drawPixel(xx, yy, (row[xx >> 3] & (0x80 >> (xx & 7))) ? GxEPD_BLACK : GxEPD_WHITE);
  }
}

struct Rc { int x, y, w, h; };

static std::vector<Rc> diffRects() {
  const uint8_t* a = cv->getBuffer(); std::vector<Rc> out;
  int start = -1, last = -100, lo = 0, hi = 0;
  for (int y = 0; y < DH; y++) {
    const uint8_t* r = a + y * ROWB; const uint8_t* p = prevBuf + y * ROWB;
    if (!memcmp(r, p, ROWB)) continue;
    int l = 0; while (r[l] == p[l]) l++;
    int h = ROWB - 1; while (r[h] == p[h]) h--;
    if (start >= 0 && y - last <= 16) { lo = min(lo, l); hi = max(hi, h); last = y; }
    else { if (start >= 0) out.push_back({lo * 8, start, (hi - lo + 1) * 8, last - start + 1}); start = last = y; lo = l; hi = h; }
  }
  if (start >= 0) out.push_back({lo * 8, start, (hi - lo + 1) * 8, last - start + 1});
  // panel RAM is addressed in 8-pixel units along portrait Y
  for (auto& r : out) { int y0 = r.y & ~7, y1 = min(DH, (r.y + r.h + 7) & ~7); r.y = y0; r.h = y1 - y0; }
  for (size_t i = 1; i < out.size();) {   // merge bands that touch after alignment
    Rc& p = out[i - 1]; Rc& c = out[i];
    if (c.y <= p.y + p.h) { int x0 = min(p.x, c.x), x1 = max(p.x + p.w, c.x + c.w), y1 = max(p.y + p.h, c.y + c.h); p = {x0, p.y, x1 - x0, y1 - p.y}; out.erase(out.begin() + i); }
    else i++;
  }
  if (out.size() > 5) {
    int x0 = DW, y0 = DH, x1 = 0, y1 = 0;
    for (auto& r : out) { x0 = min(x0, r.x); y0 = min(y0, r.y); x1 = max(x1, r.x + r.w); y1 = max(y1, r.y + r.h); }
    out = {{x0, y0, x1 - x0, y1 - y0}};
  }
  return out;
}

void dispCommit(bool screenChanged) {
  if (!havePrev || fullReq || (screenChanged && gSinceFull >= gSet.fullEvery)) {
    const char* why = !havePrev ? "BOOT" : fullReq ? fullWhy : "GHOSTING CLEANUP";
    epd.setFullWindow(); epd.firstPage(); do { blit(0, 0, DW, DH); } while (epd.nextPage());
    gFulls++; gSinceFull = 0; fullReq = false;
    Serial.printf("[epd] full refresh · %s\n", why);
  } else {
    auto rects = diffRects();
    if (rects.empty()) return;
    long area = 0;
    for (auto& r : rects) {
      epd.setPartialWindow(r.x, r.y, r.w, r.h);
      epd.firstPage(); do { blit(r.x, r.y, r.w, r.h); } while (epd.nextPage());
      area += (long)r.w * r.h;
    }
    gPartials++; gSinceFull++;
    Serial.printf("[epd] partial · %u window(s) · %ld%% of panel · %u/%u\n", (unsigned)rects.size(), max(1L, area * 100 / (DW * DH)), gSinceFull, gSet.fullEvery);
  }
  epd.powerOff();
  memcpy(prevBuf, cv->getBuffer(), BYTES);
  havePrev = true;
}

// ---------- text ----------
static const UIFont& FM(FontId f) { return UI_FONTS[f]; }
static const GFXglyph* glyph(const GFXfont* f, uint8_t c) { return (c < f->first || c > f->last) ? nullptr : &f->glyph[c - f->first]; }

// UTF-8 -> 8-bit glyph codes. Generated fonts carry Latin-1 plus UI symbols in 0x7F..0x89.
static void putCp(String& o, uint32_t cp) {
  uint8_t c = '?';
  if (cp < 0x80) c = cp;
  else if (cp >= 0xA0 && cp <= 0xFF) c = cp;
  else switch (cp) {
    case 0x2026: c = 0x7F; break; case 0x203A: c = 0x80; break; case 0x25B2: c = 0x81; break; case 0x25BC: c = 0x82; break;
    case 0x2014: c = 0x83; break; case 0x2013: c = 0x84; break; case 0x25CF: c = 0x85; break; case 0x25B6: c = 0x86; break;
    case 0x2192: c = 0x87; break; case 0x2191: c = 0x88; break; case 0x2193: c = 0x89; break;
    case 0x2018: case 0x2019: c = '\''; break; case 0x201C: case 0x201D: c = '"'; break;
  }
#ifdef UI_FONTS_LATIN1
  o += (char)c;
#else
  static const char* SYM[] = {"...", ">", "^", "v", "-", "-", "*", ">", "->", "^", "v"};
  static const char FOLD[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if (c < 0x7F) o += (char)c;
  else if (c <= 0x89) o += SYM[c - 0x7F];
  else if (c >= 0xC0) o += FOLD[c - 0xC0];
  else if (c == 0xB7) o += '.';
  else o += '?';
#endif
}

static String glyphs(const String& s) {
  String o; o.reserve(s.length());
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p) {
    uint32_t cp = *p++;
    if (cp >= 0xF0) { cp = (cp & 7) << 18; cp |= (*p++ & 0x3F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
    else if (cp >= 0xE0) { cp = (cp & 0x0F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
    else if (cp >= 0xC0) { cp = (cp & 0x1F) << 6; cp |= (*p++ & 0x3F); }
    putCp(o, cp);
  }
  return o;
}

static int gw(const String& g, FontId f, int track) {
  const UIFont& u = FM(f); int w = 0, n = 0;
  for (size_t i = 0; i < g.length(); i++) { auto* gl = glyph(u.gfx, (uint8_t)g[i]); if (gl) { w += gl->xAdvance * u.scale + track; n++; } }
  return n ? w - track : 0;
}

int textW(const String& s, FontId f, int track) { return gw(glyphs(s), f, track); }
int textAsc(FontId f) {
  const UIFont& u = FM(f); auto* g = glyph(u.gfx, 'H'); if (!g) g = glyph(u.gfx, '0');
  return g ? -g->yOffset * u.scale : u.gfx->yAdvance * u.scale * 7 / 10;
}
int textLineH(FontId f) { return FM(f).gfx->yAdvance * FM(f).scale; }

int drawText(int x, int y, const String& s, FontId f, int track, int maxW, uint16_t c) {
  String g = glyphs(s); const UIFont& u = FM(f);
  if (maxW > 0 && gw(g, f, track) > maxW) {
    String e; putCp(e, 0x2026);
    int ew = gw(e, f, track) + track;
    while (g.length() && gw(g, f, track) + ew > maxW) g.remove(g.length() - 1);
    while (g.endsWith(" ")) g.remove(g.length() - 1);
    g += e;
  }
  int base = y + textAsc(f), cx = x;
  cv->setFont(u.gfx);   // without this the canvas falls back to the built-in 5x7 font
  for (size_t i = 0; i < g.length(); i++) {
    uint8_t ch = g[i]; auto* gl = glyph(u.gfx, ch); if (!gl) continue;
    cv->drawChar(cx, base, ch, c, c, u.scale);
    cx += gl->xAdvance * u.scale + track;
  }
  return cx - x;
}

std::vector<String> wrapText(const String& s, FontId f, int maxW, int track) {
  std::vector<String> lines; String line; int i = 0, n = s.length();
  while (i < n) {
    int j = s.indexOf(' ', i); if (j < 0) j = n;
    String word = s.substring(i, j);
    String cand = line.length() ? line + " " + word : word;
    if (textW(cand, f, track) <= maxW || !line.length()) {
      if (!line.length() && textW(word, f, track) > maxW) {   // hard-break a long word
        String part;
        for (char ch : word) { if (textW(part + ch, f, track) > maxW && part.length()) { lines.push_back(part); part = ""; } part += ch; }
        line = part;
      } else line = cand;
    } else { lines.push_back(line); line = word; }
    i = j + 1;
  }
  if (line.length()) lines.push_back(line);
  return lines;
}

void fillR(int x, int y, int w, int h, uint16_t c) { if (w > 0 && h > 0) cv->fillRect(x, y, w, h, c); }
void boxR(int x, int y, int w, int h, int t, uint16_t c) { fillR(x, y, w, t, c); fillR(x, y + h - t, w, t, c); fillR(x, y, t, h, c); fillR(x + w - t, y, t, h, c); }
void dashR(int x, int y, int w, int h) {
  for (int i = 0; i < w; i += 6) { fillR(x + i, y, min(3, w - i), 2); fillR(x + i, y + h - 2, min(3, w - i), 2); }
  for (int i = 0; i < h; i += 6) { fillR(x, y + i, 2, min(3, h - i)); fillR(x + w - 2, y + i, 2, min(3, h - i)); }
}
void discR(int cx, int cy, int r, bool fill) { if (fill) cv->fillCircle(cx, cy, r, 1); else { cv->drawCircle(cx, cy, r, 1); cv->drawCircle(cx, cy, r - 1, 1); } }
