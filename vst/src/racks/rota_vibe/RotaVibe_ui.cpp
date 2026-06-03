/* RotaVibe rack UI — shared rack_ui template. */
#include "RotaVibeParams.h"
#define RACK_COUNT   kParamCount
#define RACK_TITLE   "ROTA VIBE"
#define RACK_NAMES   kRotaVibeNames
#define RACK_DEFS    kRotaVibeDef
#define RACK_ACR 205
#define RACK_ACG 135
#define RACK_ACB 120
#define RACK_KNOBS { {0.155f,0.48f,0.024f}, {0.265f,0.48f,0.024f}, {0.375f,0.48f,0.024f}, {0.485f,0.48f,0.024f} }
#include "../_shared/rack_ui.hpp"
