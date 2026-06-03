/* StudioDelay rack UI — shared rack_ui template. Five knobs on a blue panel. */
#include "StudioDelayParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO DELAY"
#define RACK_NAMES   kStudioDelayNames
#define RACK_DEFS    kStudioDelayDef
#define RACK_ACR 105
#define RACK_ACG 135
#define RACK_ACB 205
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.237f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.402f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
