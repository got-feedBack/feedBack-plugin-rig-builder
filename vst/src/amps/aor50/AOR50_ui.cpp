/* AOR50 fallback DPF UI — shared pedal_ui template. The real in-app face is
 * drawn by pedal_canvas.js (P.aor50, "Raney" brand). Knob count + labels come
 * from the plugin params (13: the full AOR50 panel). */
#include "AOR50Params.h"
#define PEDAL_TITLE  "RANEY AOR50"
#define PEDAL_NAMES  kAOR50Names
#define PEDAL_DEFS   kAOR50Def
#define PEDAL_ACR 26
#define PEDAL_ACG 26
#define PEDAL_ACB 28
#define PEDAL_ARCR 210
#define PEDAL_ARCG 214
#define PEDAL_ARCB 220
#define PEDAL_W 640
#define PEDAL_H 430
// 13 controls in a 5-col grid (param-id order: Channel, AOR Preamp/Master/Bright,
// Ch1 Preamp/Master/Bright, Bass, Middle, Treble, Deep, Mid Boost, Presence).
#define PEDAL_KNOBS { \
  {0.10f,0.16f,0.05f}, {0.30f,0.16f,0.05f}, {0.50f,0.16f,0.05f}, {0.70f,0.16f,0.05f}, {0.90f,0.16f,0.05f}, \
  {0.10f,0.46f,0.05f}, {0.30f,0.46f,0.05f}, {0.50f,0.46f,0.05f}, {0.70f,0.46f,0.05f}, {0.90f,0.46f,0.05f}, \
  {0.10f,0.76f,0.05f}, {0.30f,0.76f,0.05f}, {0.50f,0.76f,0.05f} }
#include "../../_shared/pedal_ui.hpp"
