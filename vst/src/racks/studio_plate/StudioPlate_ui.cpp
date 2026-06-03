/* StudioPlate rack UI — shared rack_ui template. Four knobs (Time/Tone/Depth/Mix) on
 * a tinted sub-panel sampled from the Rocksmith art. */
#include "../_shared/reverb_params.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO PLATE"
#define RACK_NAMES   kReverbNames
#define RACK_DEFS    kReverbDef
#define RACK_ACR 200
#define RACK_ACG 180
#define RACK_ACB 168
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.265f,0.48f,0.024f}, {0.375f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
