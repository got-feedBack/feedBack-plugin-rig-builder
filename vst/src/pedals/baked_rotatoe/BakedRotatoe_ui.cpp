/* BakedRotatoe stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_BakedRotatoe); knob count + labels from the plugin params. */
#include "BakedRotatoeParams.h"
#define PEDAL_TITLE  "BAKED ROTATOE"
#define PEDAL_NAMES  kBakedRotatoeNames
#define PEDAL_DEFS   kBakedRotatoeDef
#define PEDAL_ACR 48
#define PEDAL_ACG 105
#define PEDAL_ACB 70
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.18f,0.19f,0.085f}, {0.39f,0.19f,0.085f}, {0.61f,0.19f,0.085f}, {0.82f,0.19f,0.085f} }
#include "../_shared/pedal_ui.hpp"
