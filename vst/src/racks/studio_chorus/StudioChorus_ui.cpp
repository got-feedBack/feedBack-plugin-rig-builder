/* StudioChorus rack UI - shared rack_ui template. Boss RCE-10 Chorus Ensemble:
 * the 5 real front-panel pots only — Rate, Depth, Effect Level, Effect EQ, Pre Delay. */
#include "StudioChorusParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO CHORUS"
#define RACK_NAMES   kStudioChorusNames
#define RACK_DEFS    kStudioChorusDef
#define RACK_ACR 120
#define RACK_ACG 165
#define RACK_ACB 205
// enum order: Rate, Depth, Effect Level, Effect EQ, Pre Delay
#define RACK_KNOBS { \
    {0.180f,0.55f,0.026f}, {0.320f,0.55f,0.026f}, {0.460f,0.55f,0.026f}, \
    {0.600f,0.55f,0.026f}, {0.740f,0.55f,0.026f} }
#include "../_shared/rack_ui.hpp"
