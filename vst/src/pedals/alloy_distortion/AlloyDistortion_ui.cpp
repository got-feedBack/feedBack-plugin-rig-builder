/* AlloyDistortion stompbox UI - shared pedal_ui template. Colour sampled from
 * the game art (Pedal_MetalDistortion); controls match the HM-2 panel. */
#include "AlloyDistortionParams.h"
#define PEDAL_TITLE  "ALLOY DISTORTION"
#define PEDAL_NAMES  kAlloyDistortionNames
#define PEDAL_DEFS   kAlloyDistortionDef
#define PEDAL_ACR 105
#define PEDAL_ACG 24
#define PEDAL_ACB 28
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.095f}, {0.42f,0.20f,0.095f}, {0.62f,0.20f,0.095f}, {0.82f,0.20f,0.095f} }
#include "../_shared/pedal_ui.hpp"
