/* EightiesFlanger stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_80sFlanger); knob count + labels from the plugin params. */
#include "EightiesFlangerParams.h"
#define PEDAL_TITLE  "EIGHTIES FLANGER"
#define PEDAL_NAMES  kEightiesFlangerNames
#define PEDAL_DEFS   kEightiesFlangerDef
#define PEDAL_ACR 105
#define PEDAL_ACG 135
#define PEDAL_ACB 164
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.086f}, {0.39f,0.19f,0.086f}, {0.61f,0.19f,0.086f}, {0.82f,0.19f,0.086f} }
#include "../_shared/pedal_ui.hpp"
