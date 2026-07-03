/* StudioChamber rack UI — shared rack_ui template. Four knobs (Time/Tone/Depth/Mix) on
 * a tinted sub-panel sampled from the game art. */
#include "../../_shared/reverb_params.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO CHAMBER"
#define RACK_NAMES   kReverbNames
#define RACK_DEFS    kReverbDef
#define RACK_ACR 140
#define RACK_ACG 175
#define RACK_ACB 200
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.265f,0.48f,0.024f}, {0.375f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../../_shared/rack_ui.hpp"
