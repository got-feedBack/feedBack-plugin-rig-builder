/* TW26 stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Amp_TW26); knob count + labels from the plugin params. */
#include "TW26Params.h"
#define PEDAL_TITLE  "Bender Deluxe"
#define PEDAL_NAMES  kTW26Names
#define PEDAL_DEFS   kTW26Def
#define PEDAL_ACR 113
#define PEDAL_ACG 90
#define PEDAL_ACB 67
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 420
#define PEDAL_H 320
// 5E3 panel: 6 controls in a 3-col x 2-row grid (param-id order:
// Tone InstVol MicVol / Bright Bass Presence). Bright/Bass/Presence have no
// real front knob (hidden), but the DAW UI still exposes them.
#define PEDAL_KNOBS { \
  {0.18f,0.30f,0.075f}, {0.50f,0.30f,0.075f}, {0.82f,0.30f,0.075f}, \
  {0.18f,0.74f,0.075f}, {0.50f,0.74f,0.075f}, {0.82f,0.74f,0.075f} }
#include "../../_shared/pedal_ui.hpp"
