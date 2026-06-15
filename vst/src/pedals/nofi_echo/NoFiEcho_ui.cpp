/* NoFiEcho stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_NoFiEcho); knob count + labels from the plugin params. */
#include "NoFiEchoParams.h"
#define PEDAL_TITLE  "NO FI ECHO"
#define PEDAL_NAMES  kNoFiEchoNames
#define PEDAL_DEFS   kNoFiEchoDef
#define PEDAL_ACR 76
#define PEDAL_ACG 68
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
