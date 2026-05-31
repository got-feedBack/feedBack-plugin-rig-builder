/* AcousticSimulator stompbox UI — shared pedal_ui template. */
#include "AcousticSimulatorParams.h"
#define PEDAL_TITLE  "ACOUSTIC"
#define PEDAL_NAMES  kAcousticSimulatorNames
#define PEDAL_DEFS   kAcousticSimulatorDef
#define PEDAL_ACR 31
#define PEDAL_ACG 85
#define PEDAL_ACB 105
#define PEDAL_KNOBS { {0.30f,0.16f,0.095f}, {0.70f,0.16f,0.095f}, {0.30f,0.40f,0.095f}, {0.70f,0.40f,0.095f} }
#include "../_shared/pedal_ui.hpp"
