/* ValveEcho stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_ValveEcho); knob count + labels from the plugin params. */
#include "ValveEchoParams.h"
#define PEDAL_TITLE  "VALVE ECHO"
#define PEDAL_NAMES  kValveEchoNames
#define PEDAL_DEFS   kValveEchoDef
#define PEDAL_ACR 158
#define PEDAL_ACG 82
#define PEDAL_ACB 159
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
