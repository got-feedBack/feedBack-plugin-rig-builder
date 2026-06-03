/* VintageDistortion stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_VintageDistortion); knob count + labels from the plugin params. */
#include "VintageDistortionParams.h"
#define PEDAL_TITLE  "VINTAGE DISTORTION"
#define PEDAL_NAMES  kVintageDistortionNames
#define PEDAL_DEFS   kVintageDistortionDef
#define PEDAL_ACR 164
#define PEDAL_ACG 108
#define PEDAL_ACB 35
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
