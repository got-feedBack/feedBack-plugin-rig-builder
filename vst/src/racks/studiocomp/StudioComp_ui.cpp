/* Studio Comp — 1U rack UI (shared rack_ui template). */
#include "StudioCompParams.h"
#define RACK_COUNT  cNumParams
#define RACK_TITLE  "STUDIO COMP"
#define RACK_NAMES  kCompNames
#define RACK_NO_DEFS
#define RACK_ACR 226
#define RACK_ACG 150
#define RACK_ACB 28
#define RACK_KNOBS { {0.155f,0.50f,0.034f}, {0.235f,0.50f,0.034f}, {0.315f,0.50f,0.034f}, {0.395f,0.50f,0.034f}, {0.475f,0.50f,0.034f} }
#include "../_shared/rack_ui.hpp"
