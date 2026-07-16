/* Uni-Vibe chassis controls. Speed remains a host parameter because the real
 * unit drives it from the external foot controller; Mode is fixed to Chorus. */
#include "AmpVibeParams.h"
#define PEDAL_TITLE  "UNI-VIBE"
#define PEDAL_NAMES  kAmpVibeNames
#define PEDAL_DEFS   kAmpVibeDef
#define PEDAL_ACR 152
#define PEDAL_ACG 152
#define PEDAL_ACB 155
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_VISIBLE_COUNT 2
#define PEDAL_PARAM_IDS { kIntensity, kVolume }
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
