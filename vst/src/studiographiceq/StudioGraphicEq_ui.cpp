/* Studio Graphic EQ — 1U rack UI (shared rack_ui template). */
#include "SGEqParams.h"
#define RACK_COUNT  gNumParams
#define RACK_TITLE  "GRAPHIC EQ"
#define RACK_NAMES  kSgNames
#define RACK_NO_DEFS
#define RACK_ACR 40
#define RACK_ACG 110
#define RACK_ACB 160
#define RACK_KNOBS { \
  {0.155f,0.34f,0.027f}, {0.235f,0.34f,0.027f}, {0.315f,0.34f,0.027f}, {0.395f,0.34f,0.027f}, {0.475f,0.34f,0.027f}, \
  {0.155f,0.68f,0.027f}, {0.235f,0.68f,0.027f}, {0.315f,0.68f,0.027f}, {0.395f,0.68f,0.027f}, {0.475f,0.68f,0.027f} }
#include "../_shared/rack_ui.hpp"
