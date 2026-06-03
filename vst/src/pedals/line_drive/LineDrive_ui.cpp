/* LineDrive stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_LineDrive); knob count + labels from the plugin params. */
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
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
