/* TW26 stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_TW26); knob count + labels from the plugin params. */
#include "TW26Params.h"
#define PEDAL_TITLE  "TW26"
#define PEDAL_NAMES  kTW26Names
#define PEDAL_DEFS   kTW26Def
#define PEDAL_ACR 113
#define PEDAL_ACG 90
#define PEDAL_ACB 67
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.25f,0.15f,0.082f}, {0.50f,0.15f,0.082f}, {0.75f,0.15f,0.082f}, {0.36f,0.36f,0.082f}, {0.64f,0.36f,0.082f} }
#include "../_shared/pedal_ui.hpp"
