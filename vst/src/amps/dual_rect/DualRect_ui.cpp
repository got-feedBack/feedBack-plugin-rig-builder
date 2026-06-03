/* DualRect stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_CA100); knob count + labels from the plugin params. */
#include "DualRectParams.h"
#define PEDAL_TITLE  "DUAL RECT"
#define PEDAL_NAMES  kDualRectNames
#define PEDAL_DEFS   kDualRectDef
#define PEDAL_ACR 122
#define PEDAL_ACG 118
#define PEDAL_ACB 122
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.25f,0.15f,0.082f}, {0.50f,0.15f,0.082f}, {0.75f,0.15f,0.082f}, {0.36f,0.36f,0.082f}, {0.64f,0.36f,0.082f} }
#include "../_shared/pedal_ui.hpp"
