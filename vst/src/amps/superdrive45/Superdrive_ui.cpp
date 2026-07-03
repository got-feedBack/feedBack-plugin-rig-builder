/* Superdrive45 fallback DPF UI — shared pedal_ui template. The real in-app face
 * is drawn by pedal_canvas.js (P.superdrive45, "Ganddi" brand, purple panel).
 * Knob count + labels come from the plugin params (9: 6 knobs + 3 pulls). */
#include "SuperdriveParams.h"
#define PEDAL_TITLE  "GANDDI SUPERDRIVE 45"
#define PEDAL_NAMES  kSuperNames
#define PEDAL_DEFS   kSuperDef
#define PEDAL_ACR 120
#define PEDAL_ACG 58
#define PEDAL_ACB 128
#define PEDAL_ARCR 24
#define PEDAL_ARCG 22
#define PEDAL_ARCB 26
#define PEDAL_W 620
#define PEDAL_H 300
// 9 controls (Master, Bass, Mid, Treble, Drive, Rhythm, Channel, Modern, Brite)
#define PEDAL_KNOBS { \
  {0.09f,0.30f,0.046f}, {0.20f,0.30f,0.046f}, {0.31f,0.30f,0.046f}, \
  {0.42f,0.30f,0.046f}, {0.53f,0.30f,0.046f}, {0.64f,0.30f,0.046f}, \
  {0.75f,0.30f,0.040f}, {0.84f,0.30f,0.040f}, {0.93f,0.30f,0.040f} }
#include "../../_shared/pedal_ui.hpp"
