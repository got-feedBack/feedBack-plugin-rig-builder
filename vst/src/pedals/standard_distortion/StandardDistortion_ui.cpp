/* StandardDistortion stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_Distortion); knob count + labels from the plugin params. */
#include "StandardDistortionParams.h"
#define PEDAL_TITLE  "STANDARD DISTORTION"
#define PEDAL_NAMES  kStandardDistortionNames
#define PEDAL_DEFS   kStandardDistortionDef
#define PEDAL_ACR 52
#define PEDAL_ACG 107
#define PEDAL_ACB 121
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.24f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.76f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
