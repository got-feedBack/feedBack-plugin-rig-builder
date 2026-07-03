/* ModernFlanger stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_ModernFlanger); knob count + labels from the plugin params. */
#include "ModernFlangerParams.h"
#define PEDAL_TITLE  "FM108 CLUSTER FLUX"
#define PEDAL_NAMES  kModernFlangerNames
#define PEDAL_DEFS   kModernFlangerDef
#define PEDAL_ACR 139
#define PEDAL_ACG 129
#define PEDAL_ACB 142
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.16f,0.060f}, {0.42f,0.16f,0.052f}, {0.62f,0.16f,0.060f}, {0.82f,0.16f,0.052f}, {0.22f,0.34f,0.052f}, {0.42f,0.34f,0.052f}, {0.62f,0.34f,0.052f}, {0.82f,0.34f,0.052f}, {0.52f,0.52f,0.060f} }
#include "../_shared/pedal_ui.hpp"
