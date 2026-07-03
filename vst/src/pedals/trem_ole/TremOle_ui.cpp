/* TremOle stompbox UI - shared pedal template. */
#include "TremOleParams.h"
#define PEDAL_TITLE "TREM-OLE"
#define PEDAL_NAMES kTremOleNames
#define PEDAL_DEFS kTremOleDef
#define PEDAL_ACR 112
#define PEDAL_ACG 114
#define PEDAL_ACB 118
#define PEDAL_KNOBS { {0.14f,0.22f,0.078f}, {0.32f,0.22f,0.078f}, {0.50f,0.22f,0.078f}, {0.68f,0.22f,0.078f}, {0.86f,0.22f,0.078f} }  /* 5 knobs incl. Mode (was 4 -> Mode was invisible) */
#include "../_shared/pedal_ui.hpp"
