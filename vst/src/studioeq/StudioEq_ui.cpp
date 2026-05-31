/* Studio EQ — 1U rack UI (shared rack_ui template). */
#include "StudioEqParams.h"
#define RACK_COUNT  kNumParams
#define RACK_TITLE  "PARAMETRIC EQ"
#define RACK_NAMES  kSeqNames
#define RACK_NO_DEFS
#define RACK_ACR 168
#define RACK_ACG 30
#define RACK_ACB 120
#define RACK_KNOBS { \
  {0.155f,0.34f,0.027f}, {0.235f,0.34f,0.027f}, {0.315f,0.34f,0.027f}, {0.395f,0.34f,0.027f}, {0.475f,0.34f,0.027f}, \
  {0.155f,0.68f,0.027f}, {0.235f,0.68f,0.027f}, {0.315f,0.68f,0.027f}, {0.395f,0.68f,0.027f}, {0.475f,0.68f,0.027f} }
#include "../_shared/rack_ui.hpp"
