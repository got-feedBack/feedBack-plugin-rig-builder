/* AnalogDelay stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_AnalogueDelay); knob count + labels from the plugin params. */
#include "AnalogDelayParams.h"
#define PEDAL_TITLE  "ANALOG DELAY"
#define PEDAL_NAMES  kAnalogDelayNames
#define PEDAL_DEFS   kAnalogDelayDef
#define PEDAL_ACR 157
#define PEDAL_ACG 154
#define PEDAL_ACB 149
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
