/* ShredZone stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_ShredZone); knob count + labels from the plugin params. */
#include "ShredZoneParams.h"
#define PEDAL_TITLE  "SHRED ZONE"
#define PEDAL_NAMES  kShredZoneNames
#define PEDAL_DEFS   kShredZoneDef
#define PEDAL_ACR 96
#define PEDAL_ACG 93
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.085f}, {0.39f,0.19f,0.085f}, {0.61f,0.19f,0.085f}, {0.82f,0.19f,0.085f} }
#include "../_shared/pedal_ui.hpp"
