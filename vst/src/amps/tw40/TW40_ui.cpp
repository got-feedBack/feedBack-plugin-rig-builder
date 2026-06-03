/* TW40 stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_TW40); knob count + labels from the plugin params. */
#include "TW40Params.h"
#define PEDAL_TITLE  "TW40"
#define PEDAL_NAMES  kTW40Names
#define PEDAL_DEFS   kTW40Def
#define PEDAL_ACR 105
#define PEDAL_ACG 91
#define PEDAL_ACB 82
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.25f,0.15f,0.082f}, {0.50f,0.15f,0.082f}, {0.75f,0.15f,0.082f}, {0.36f,0.36f,0.082f}, {0.64f,0.36f,0.082f} }
#include "../_shared/pedal_ui.hpp"
