/* Big Buzz stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_BuzzToo); knob count + labels from the plugin params. */
#include "BigBuzzParams.h"
#define PEDAL_TITLE  "BIG BUZZ"
#define PEDAL_NAMES  kBigBuzzNames
#define PEDAL_DEFS   kBigBuzzDef
#define PEDAL_ACR 123
#define PEDAL_ACG 110
#define PEDAL_ACB 73
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
