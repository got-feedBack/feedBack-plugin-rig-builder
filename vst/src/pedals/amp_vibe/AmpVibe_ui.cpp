/* AmpVibe stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_AmpVibe); knob count + labels from the plugin params. */
#include "AmpVibeParams.h"
#define PEDAL_TITLE  "AMP VIBE"
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
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
