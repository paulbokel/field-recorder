#pragma once
// Font roles used by the UI. Run tools/make_fonts.py to generate Literata + Geist Mono
// (writes src/fonts/ui_fonts.h); until then the Adafruit FreeFonts below are used.
#include <Adafruit_GFX.h>
struct UIFont { const GFXfont* gfx; uint8_t scale; };

#if __has_include("fonts/ui_fonts.h")
#include "fonts/ui_fonts.h"
#else
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
static const UIFont UI_FONTS[F_COUNT] = {
  {&FreeSerif24pt7b, 1},     // F_SERIF_XL  home labels
  {&FreeSerif18pt7b, 1},     // F_SERIF_L   screen heads
  {&FreeSerif12pt7b, 1},     // F_SERIF_M   rows
  {&FreeSerif12pt7b, 1},     // F_SERIF_S   reader
  {&FreeMonoBold24pt7b, 2},  // F_MONO_XXL  timers
  {&FreeMonoBold24pt7b, 2},  // F_MONO_XL   keyboard wheel centre
  {&FreeMono18pt7b, 1},      // F_MONO_L
  {&FreeMono9pt7b, 1},       // F_MONO_M
  {&FreeMono9pt7b, 1},       // F_MONO_S    captions
};
#endif
