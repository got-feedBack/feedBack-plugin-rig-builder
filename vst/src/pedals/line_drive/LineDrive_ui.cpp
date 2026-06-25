/* LineDrive stompbox UI - shared pedal_ui template. Colour sampled from the
 * game art (Pedal_LineDrive); controls match the OS-2 panel. */
#include "LineDriveParams.h"
#define PEDAL_TITLE  "LINE DRIVE"
#define PEDAL_NAMES  kLineDriveNames
#define PEDAL_DEFS   kLineDriveDef
#define PEDAL_ACR 195
#define PEDAL_ACG 165
#define PEDAL_ACB 15
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.095f}, {0.42f,0.20f,0.095f}, {0.62f,0.20f,0.095f}, {0.82f,0.20f,0.095f} }
#include "../_shared/pedal_ui.hpp"
