/* BassMultiComp stompbox UI — shared pedal_ui template. */
#include "BassMultiCompParams.h"
#define PEDAL_TITLE  "MB COMP"
#define PEDAL_NAMES  kBassMultiCompNames
#define PEDAL_DEFS   kBassMultiCompDef
#define PEDAL_ACR 140
#define PEDAL_ACG 143
#define PEDAL_ACB 148
#define PEDAL_KNOBS { {0.27f,0.17f,0.115f}, {0.50f,0.40f,0.095f}, {0.73f,0.17f,0.115f} }
#include "../_shared/pedal_ui.hpp"
