/* StereoAnalogVibe rack UI — shared rack_ui template. */
#include "StereoAnalogVibeParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STEREO VIBRATO"
#define RACK_NAMES   kStereoAnalogVibeNames
#define RACK_DEFS    kStereoAnalogVibeDef
#define RACK_ACR 140
#define RACK_ACG 135
#define RACK_ACB 195
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
