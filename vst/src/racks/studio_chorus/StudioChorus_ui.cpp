/* StudioChorus rack UI - shared rack_ui template. Boss RCE-10 Chorus Ensemble:
 * Pre Delay, Rate/Depth, Effect EQ/Level plus RS-compatible Low Cut/Stereo. */
#include "StudioChorusParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "STUDIO CHORUS"
#define RACK_NAMES   kStudioChorusNames
#define RACK_DEFS    kStudioChorusDef
#define RACK_ACR 120
#define RACK_ACG 165
#define RACK_ACB 205
// enum order: Rate, Depth, Effect Level, Low Cut | Effect EQ, Stereo, Pre Delay
#define RACK_KNOBS { \
    {0.160f,0.40f,0.023f}, {0.265f,0.40f,0.023f}, {0.370f,0.40f,0.023f}, {0.475f,0.40f,0.023f}, \
    {0.215f,0.72f,0.023f}, {0.320f,0.72f,0.023f}, {0.425f,0.72f,0.023f} }
#include "../_shared/rack_ui.hpp"
