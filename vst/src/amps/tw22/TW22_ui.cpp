/* TW22 stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_TW22); knob count + labels from the plugin params. */
#include "TW22Params.h"
#define PEDAL_TITLE  "TW22"
#define PEDAL_NAMES  kTW22Names
#define PEDAL_DEFS   kTW22Def
#define PEDAL_ACR 91
#define PEDAL_ACG 92
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.085f}, {0.39f,0.19f,0.085f}, {0.61f,0.19f,0.085f}, {0.82f,0.19f,0.085f} }
#include "../_shared/pedal_ui.hpp"
