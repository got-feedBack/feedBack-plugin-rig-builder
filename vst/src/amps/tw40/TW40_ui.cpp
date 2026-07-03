/* TW40 fallback DPF UI — shared pedal_ui template. The real in-app face is
 * drawn by pedal_canvas.js (P.tw40, "Bender" brand, tweed Bassman). Knob count +
 * labels come from the plugin params (7: the full 5F6-A panel). */
#include "TW40Params.h"
#define PEDAL_TITLE  "BENDER BASSMAN"
#define PEDAL_NAMES  kTW40Names
#define PEDAL_DEFS   kTW40Def
#define PEDAL_ACR 196
#define PEDAL_ACG 168
#define PEDAL_ACB 104
#define PEDAL_ARCR 30
#define PEDAL_ARCG 28
#define PEDAL_ARCB 26
#define PEDAL_W 560
#define PEDAL_H 300
// 7 controls (Input, Bright Vol, Normal Vol, Treble, Bass, Middle, Presence)
#define PEDAL_KNOBS { \
  {0.10f,0.30f,0.058f}, {0.24f,0.30f,0.058f}, {0.38f,0.30f,0.058f}, {0.52f,0.30f,0.058f}, \
  {0.66f,0.30f,0.058f}, {0.80f,0.30f,0.058f}, {0.93f,0.30f,0.058f} }
#include "../../_shared/pedal_ui.hpp"
