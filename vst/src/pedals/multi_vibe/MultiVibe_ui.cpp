/* MultiVibe stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_MultiVibe); knob count + labels from the plugin params. */
#include "MultiVibeParams.h"
#define PEDAL_TITLE  "MULTI VIBE"
#define PEDAL_NAMES  kMultiVibeNames
#define PEDAL_DEFS   kMultiVibeDef
#define PEDAL_ACR 121
#define PEDAL_ACG 90
#define PEDAL_ACB 101
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
