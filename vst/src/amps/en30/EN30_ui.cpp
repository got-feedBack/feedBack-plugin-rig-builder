/* EN30 stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Amp_EN30); knob count + labels from the plugin params. */
#include "EN30Params.h"
#define PEDAL_TITLE  "BOX AC30"
#define PEDAL_NAMES  kEN30Names
#define PEDAL_DEFS   kEN30Def
#define PEDAL_ACR 95
#define PEDAL_ACG 95
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 640
#define PEDAL_H 300
// AC30C2 controls plus fallback Cab Sim in a 7/6 grid (param-id order:
// NormalVol TBVol Treble Bass RevTone RevLevel Speed / Depth ToneCut Master
// Input Bright CabSim).
#define PEDAL_KNOBS { \
  {0.070f,0.30f,0.045f}, {0.213f,0.30f,0.045f}, {0.357f,0.30f,0.045f}, {0.500f,0.30f,0.045f}, {0.643f,0.30f,0.045f}, {0.787f,0.30f,0.045f}, {0.930f,0.30f,0.045f}, \
  {0.095f,0.74f,0.045f}, {0.257f,0.74f,0.045f}, {0.419f,0.74f,0.045f}, {0.581f,0.74f,0.045f}, {0.743f,0.74f,0.045f}, {0.905f,0.74f,0.045f} }
#include "../../_shared/pedal_ui.hpp"
