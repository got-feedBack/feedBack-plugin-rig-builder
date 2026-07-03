/* BZ-1 stompbox UI — shared pedal_ui template. */
#include "BZ1Params.h"
#define PEDAL_TITLE  "BZ-1"
#define PEDAL_NAMES  kBZ1Names
#define PEDAL_DEFS   kBZ1Def
#define PEDAL_ACR 105
#define PEDAL_ACG 68
#define PEDAL_ACB 28
#define PEDAL_W 320
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.25f,0.17f,0.112f}, {0.50f,0.17f,0.112f}, {0.75f,0.17f,0.112f} }
#include "../_shared/pedal_ui.hpp"
