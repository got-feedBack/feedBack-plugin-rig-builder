/* StudioFlanger rack UI — shared rack_ui template. Five knobs on a gold panel
 * (colour sampled from the RS art). */
#include "StudioFlangerParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO FLANGER"
#define RACK_NAMES   kStudioFlangerNames
#define RACK_DEFS    kStudioFlangerDef
#define RACK_ACR 205
#define RACK_ACG 170
#define RACK_ACB 75
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.237f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.402f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
