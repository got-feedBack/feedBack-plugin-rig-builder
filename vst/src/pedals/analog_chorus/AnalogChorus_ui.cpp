/* AnalogChorus stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_VintageChorus); knob count + labels from the plugin params. */
#include "AnalogChorusParams.h"
#define PEDAL_TITLE  "ANALOG CHORUS"
#define PEDAL_NAMES  kAnalogChorusNames
#define PEDAL_DEFS   kAnalogChorusDef
#define PEDAL_ACR 194
#define PEDAL_ACG 174
#define PEDAL_ACB 71
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.15f,0.075f}, {0.40f,0.15f,0.075f}, {0.62f,0.15f,0.075f}, {0.30f,0.35f,0.075f}, {0.72f,0.35f,0.075f} }
#include "../_shared/pedal_ui.hpp"
