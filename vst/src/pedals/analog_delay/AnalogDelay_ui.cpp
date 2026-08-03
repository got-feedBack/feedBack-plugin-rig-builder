/* AnalogDelay stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_AnalogueDelay); knob count + labels from the plugin params. */
#include "AnalogDelayParams.h"
#define PEDAL_TITLE  "FM104"
#define PEDAL_NAMES  kAnalogDelayNames
#define PEDAL_DEFS   kAnalogDelayDef
#define PEDAL_ACR 157
#define PEDAL_ACG 154
#define PEDAL_ACB 149
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 520
#define PEDAL_KNOBS { {0.50f,0.16f,0.050f}, {0.50f,0.31f,0.050f}, {0.23f,0.20f,0.068f}, {0.23f,0.47f,0.060f}, {0.50f,0.48f,0.050f}, {0.23f,0.33f,0.040f}, {0.77f,0.39f,0.058f}, {0.77f,0.56f,0.058f}, {0.77f,0.20f,0.058f} }
#include "../_shared/pedal_ui.hpp"
