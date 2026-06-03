/* Octavius stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_Octavius); knob count + labels from the plugin params. */
#include "OctaviusParams.h"
#define PEDAL_TITLE  "OCTAVIUS"
#define PEDAL_NAMES  kOctaviusNames
#define PEDAL_DEFS   kOctaviusDef
#define PEDAL_ACR 141
#define PEDAL_ACG 124
#define PEDAL_ACB 54
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
