/* SynthFilter rack UI — shared rack_ui template. */
#include "SynthFilterParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "SYNTH FILTER"
#define RACK_NAMES   kSynthFilterNames
#define RACK_DEFS    kSynthFilterDef
#define RACK_ACR 150
#define RACK_ACG 185
#define RACK_ACB 130
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.237f,0.48f,0.024f}, {0.320f,0.48f,0.024f}, {0.402f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
