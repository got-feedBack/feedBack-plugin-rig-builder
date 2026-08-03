/* BobFilter stompbox UI — shared pedal_ui template. */
#include "BobFilterParams.h"
#define PEDAL_TITLE  "BOB FILTER"
#define PEDAL_NAMES  kBobFilterNames
#define PEDAL_DEFS   kBobFilterDef
#define PEDAL_ACR 173
#define PEDAL_ACG 174
#define PEDAL_ACB 173
#define PEDAL_W 340
#define PEDAL_H 440
#define PEDAL_VISIBLE_COUNT 7
#define PEDAL_PARAM_IDS { kDrive, kOutput, kCutoff, kResonance, kEnvelope, kFollowRate, kMix }
#define PEDAL_KNOBS { {0.20f,0.15f,0.062f}, {0.50f,0.15f,0.062f}, {0.80f,0.15f,0.062f}, {0.20f,0.31f,0.062f}, {0.50f,0.31f,0.062f}, {0.80f,0.31f,0.062f}, {0.50f,0.46f,0.060f} }
#include "../_shared/pedal_ui.hpp"
