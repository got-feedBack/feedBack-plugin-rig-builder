/* Chorus stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_Chorus); knob count + labels from the plugin params. */
#include "ChorusParams.h"
#define PEDAL_TITLE  "CHORUS"
#define PEDAL_NAMES  kChorusNames
#define PEDAL_DEFS   kChorusDef
#define PEDAL_ACR 76
#define PEDAL_ACG 96
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.34f,0.20f,0.105f}, {0.66f,0.20f,0.105f} }
#include "../_shared/pedal_ui.hpp"
