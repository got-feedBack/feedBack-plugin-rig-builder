/* DynamicsCompression stompbox UI — shared pedal_ui template. */
#include "DynamicsCompressionParams.h"
#define PEDAL_TITLE  "COMPRESSION"
#define PEDAL_NAMES  kDynamicsCompressionNames
#define PEDAL_DEFS   kDynamicsCompressionDef
#define PEDAL_ACR 165
#define PEDAL_ACG 24
#define PEDAL_ACB 18
#define PEDAL_KNOBS { {0.22f,0.20f,0.105f}, {0.50f,0.20f,0.105f}, {0.78f,0.20f,0.105f} }
#include "../_shared/pedal_ui.hpp"
