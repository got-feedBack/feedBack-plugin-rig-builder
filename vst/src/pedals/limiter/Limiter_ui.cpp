/* Limiter stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_Limiter); knob count + labels from the plugin params. */
#include "LimiterParams.h"
#define PEDAL_TITLE  "LIMITER"
#define PEDAL_NAMES  kLimiterNames
#define PEDAL_DEFS   kLimiterDef
#define PEDAL_ACR 51
#define PEDAL_ACG 88
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
