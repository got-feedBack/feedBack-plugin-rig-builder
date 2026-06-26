/* LoFiFilter stompbox UI — shared pedal_ui template. */
#include "LoFiFilterParams.h"
#define PEDAL_TITLE  "LO-FI FILTER"
#define PEDAL_NAMES  kLoFiFilterNames
#define PEDAL_DEFS   kLoFiFilterDef
#define PEDAL_ACR 33
#define PEDAL_ACG 24
#define PEDAL_ACB 123
#define PEDAL_W 320
#define PEDAL_H 420
#define PEDAL_KNOBS { {0.24f,0.18f,0.090f}, {0.76f,0.18f,0.090f}, {0.24f,0.38f,0.090f}, {0.76f,0.38f,0.090f} }
#include "../_shared/pedal_ui.hpp"
