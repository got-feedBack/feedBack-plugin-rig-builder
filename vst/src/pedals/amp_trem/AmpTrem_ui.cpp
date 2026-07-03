/* AmpTrem stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_AmpTrem); knob count + labels from the plugin params. */
#include "AmpTremParams.h"
#define PEDAL_TITLE  "AMP TREM"
#define PEDAL_NAMES  kAmpTremNames
#define PEDAL_DEFS   kAmpTremDef
#define PEDAL_ACR 105
#define PEDAL_ACG 95
#define PEDAL_ACB 93
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
