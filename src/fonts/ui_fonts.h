#pragma once
#define UI_FONTS_LATIN1 1
#include "SerifXL.h"
#include "SerifL.h"
#include "SerifM.h"
#include "SerifS.h"
#include "MonoXXL.h"
#include "MonoXL.h"
#include "MonoL.h"
#include "MonoM.h"
#include "MonoS.h"
static const UIFont UI_FONTS[F_COUNT] = {
  {&SerifXL, 1},
  {&SerifL, 1},
  {&SerifM, 1},
  {&SerifS, 1},
  {&MonoXXL, 1},
  {&MonoXL, 1},
  {&MonoL, 1},
  {&MonoM, 1},
  {&MonoS, 1},
};
