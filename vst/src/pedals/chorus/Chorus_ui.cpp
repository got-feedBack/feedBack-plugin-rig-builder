/* Chorus stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_Chorus); knob count + labels from the plugin params. */
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
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
