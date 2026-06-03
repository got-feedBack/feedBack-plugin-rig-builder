/* StudioPitch rack UI — shared rack_ui template. */
#include "StudioPitchParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO PITCH"
#define RACK_NAMES   kStudioPitchNames
#define RACK_DEFS    kStudioPitchDef
#define RACK_ACR 160
#define RACK_ACG 185
#define RACK_ACB 150
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.265f,0.48f,0.024f}, {0.375f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
