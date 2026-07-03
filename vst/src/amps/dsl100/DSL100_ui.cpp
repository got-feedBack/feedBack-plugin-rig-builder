/* DSL100 fallback DPF UI — shared pedal_ui template. The real in-app face is
 * drawn by pedal_canvas.js (P.dsl100, "Marsten" brand). Knob count + labels come
 * from the plugin params (19, the full DSL100HR panel). */
#include "DSL100Params.h"
#define PEDAL_TITLE  "MARSTEN DSL100"
#define PEDAL_NAMES  kDSL100Names
#define PEDAL_DEFS   kDSL100Def
#define PEDAL_ACR 24
#define PEDAL_ACG 22
#define PEDAL_ACB 22
#define PEDAL_ARCR 206
#define PEDAL_ARCG 170
#define PEDAL_ARCB 92
#define PEDAL_W 640
#define PEDAL_H 430
// 19 controls in a 5-col grid (param-id order: Channel, Classic Gain/Vol/Mode,
// Ultra Gain/Vol/Mode, Bass/Mid/Treble/ToneShift, Resonance/Presence,
// RevClassic/RevUltra, Master1/2/Sel, Output).
#define PEDAL_KNOBS { \
  {0.10f,0.13f,0.045f}, {0.28f,0.13f,0.045f}, {0.46f,0.13f,0.045f}, {0.64f,0.13f,0.045f}, {0.82f,0.13f,0.045f}, \
  {0.10f,0.36f,0.045f}, {0.28f,0.36f,0.045f}, {0.46f,0.36f,0.045f}, {0.64f,0.36f,0.045f}, {0.82f,0.36f,0.045f}, \
  {0.10f,0.59f,0.045f}, {0.28f,0.59f,0.045f}, {0.46f,0.59f,0.045f}, {0.64f,0.59f,0.045f}, {0.82f,0.59f,0.045f}, \
  {0.10f,0.82f,0.045f}, {0.28f,0.82f,0.045f}, {0.46f,0.82f,0.045f}, {0.64f,0.82f,0.045f} }
#include "../../_shared/pedal_ui.hpp"
