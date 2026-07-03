/* JC90 fallback DPF UI — shared pedal_ui template. The real in-app face is
 * drawn by pedal_canvas.js (P.jc90, "Ronald" brand). Knob count + labels come
 * from the plugin params (10: the full JC-90 panel). */
#include "JC90Params.h"
#define PEDAL_TITLE  "RONALD JC-90"
#define PEDAL_NAMES  kJC90Names
#define PEDAL_DEFS   kJC90Def
#define PEDAL_ACR 28
#define PEDAL_ACG 28
#define PEDAL_ACB 30
#define PEDAL_ARCR 210
#define PEDAL_ARCG 214
#define PEDAL_ARCB 220
#define PEDAL_W 640
#define PEDAL_H 300
// 10 controls (Distortion, Volume, Hi-Treble, Treble, Middle, Bass, Reverb,
// Rate, Depth, Chorus) in two rows of 5.
#define PEDAL_KNOBS { \
  {0.10f,0.30f,0.055f}, {0.30f,0.30f,0.055f}, {0.50f,0.30f,0.055f}, {0.70f,0.30f,0.055f}, {0.90f,0.30f,0.055f}, \
  {0.10f,0.72f,0.055f}, {0.30f,0.72f,0.055f}, {0.50f,0.72f,0.055f}, {0.70f,0.72f,0.055f}, {0.90f,0.72f,0.055f} }
#include "../../_shared/pedal_ui.hpp"
