/* StudioWahFilter rack UI — shared rack_ui template. */
#include "StudioWahFilterParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO WAH FILTER"
#define RACK_NAMES   kStudioWahFilterNames
#define RACK_DEFS    kStudioWahFilterDef
#define RACK_ACR 130
#define RACK_ACG 180
#define RACK_ACB 155
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.237f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.402f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
