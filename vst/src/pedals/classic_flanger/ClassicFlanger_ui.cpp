/* ClassicFlanger stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_ClassicFlanger); knob count + labels from the plugin params. */
#include "ClassicFlangerParams.h"
#define PEDAL_TITLE  "CLASSIC FLANGER"
#define PEDAL_NAMES  kClassicFlangerNames
#define PEDAL_DEFS   kClassicFlangerDef
#define PEDAL_ACR 70
#define PEDAL_ACG 72
#define PEDAL_ACB 104
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.086f}, {0.39f,0.19f,0.086f}, {0.61f,0.19f,0.086f}, {0.82f,0.19f,0.086f} }
#include "../_shared/pedal_ui.hpp"
