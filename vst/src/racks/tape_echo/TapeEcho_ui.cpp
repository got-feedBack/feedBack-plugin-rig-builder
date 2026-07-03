/* TapeEcho rack UI — shared rack_ui template. Five knobs on a green-grey panel. */
#include "TapeEchoParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "TAPE ECHO"
#define RACK_NAMES   kTapeEchoNames
#define RACK_DEFS    kTapeEchoDef
#define RACK_ACR 135
#define RACK_ACG 170
#define RACK_ACB 130
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.237f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.402f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
