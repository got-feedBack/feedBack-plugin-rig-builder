/* StereoPhaser rack UI — shared rack_ui template. */
#include "StereoPhaserParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STEREO PHASER"
#define RACK_NAMES   kStereoPhaserNames
#define RACK_DEFS    kStereoPhaserDef
#define RACK_ACR 90
#define RACK_ACG 175
#define RACK_ACB 178
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
