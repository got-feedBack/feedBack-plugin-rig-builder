/* DynamicsCompression stompbox UI — shared pedal_ui template. */
#include "DynamicsCompressionParams.h"
#define PEDAL_TITLE  "COMPRESSION"
#define PEDAL_NAMES  kDynamicsCompressionNames
#define PEDAL_DEFS   kDynamicsCompressionDef
#define PEDAL_ACR 165
#define PEDAL_ACG 24
#define PEDAL_ACB 18
#define PEDAL_W 320
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.28f,0.30f,0.115f}, {0.50f,0.17f,0.115f}, {0.72f,0.30f,0.115f} }
#include "../_shared/pedal_ui.hpp"
