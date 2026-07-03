/* RingMod stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_RingMod); knob count + labels from the plugin params. */
#include "RingModParams.h"
#define PEDAL_TITLE  "RING MOD"
#define PEDAL_NAMES  kRingModNames
#define PEDAL_DEFS   kRingModDef
#define PEDAL_ACR 142
#define PEDAL_ACG 121
#define PEDAL_ACB 155
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.085f}, {0.39f,0.19f,0.085f}, {0.61f,0.19f,0.085f}, {0.82f,0.19f,0.085f} }
#include "../_shared/pedal_ui.hpp"
