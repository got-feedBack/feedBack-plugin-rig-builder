/* AlloyDistortion stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_MetalDistortion); knob count + labels from the plugin params. */
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
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
