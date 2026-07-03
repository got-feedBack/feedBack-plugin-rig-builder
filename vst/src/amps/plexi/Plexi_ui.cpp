/* Plexi fallback DPF UI — shared pedal_ui template. The real in-app face is
 * drawn by pedal_canvas.js (P.plexi, "Marsten" brand, gold-panel Plexi head).
 * Knob count + labels come from the plugin params (full 1959 panel + Cab Sim). */
#include "PlexiParams.h"
#define PEDAL_TITLE  "MARSTEN PLEXI"
#define PEDAL_NAMES  kPlexiNames
#define PEDAL_DEFS   kPlexiDef
#define PEDAL_ACR 198
#define PEDAL_ACG 162
#define PEDAL_ACB 78
#define PEDAL_ARCR 26
#define PEDAL_ARCG 24
#define PEDAL_ARCB 22
#define PEDAL_W 560
#define PEDAL_H 300
// 8 controls (Presence, Bass, Middle, Treble, Loudness I, Loudness II, Input, Cab Sim)
#define PEDAL_KNOBS { \
  {0.08f,0.30f,0.046f}, {0.20f,0.30f,0.046f}, {0.32f,0.30f,0.046f}, \
  {0.44f,0.30f,0.046f}, {0.56f,0.30f,0.046f}, {0.68f,0.30f,0.046f}, \
  {0.80f,0.30f,0.046f}, {0.92f,0.30f,0.046f} }
#include "../../_shared/pedal_ui.hpp"
