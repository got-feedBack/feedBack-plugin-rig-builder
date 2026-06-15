/* Super-Buzz stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_BuzzOne); knob count + labels from the plugin params. */
#include "SuperBuzzParams.h"
#define PEDAL_TITLE  "SUPER-BUZZ"
#define PEDAL_NAMES  kSuperBuzzNames
#define PEDAL_DEFS   kSuperBuzzDef
#define PEDAL_ACR 62
#define PEDAL_ACG 80
#define PEDAL_ACB 183
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
