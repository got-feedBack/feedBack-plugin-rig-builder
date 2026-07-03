/* NoiseGate stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_NoiseGate); knob count + labels from the plugin params. */
#include "NoiseGateParams.h"
#define PEDAL_TITLE  "NOISE GATE"
#define PEDAL_NAMES  kNoiseGateNames
#define PEDAL_DEFS   kNoiseGateDef
#define PEDAL_ACR 97
#define PEDAL_ACG 99
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
