/* ModernFlanger stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_ModernFlanger); knob count + labels from the plugin params. */
#include "ModernFlangerParams.h"
#define PEDAL_TITLE  "MODERN FLANGER"
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
#define PEDAL_KNOBS { {0.18f,0.19f,0.085f}, {0.39f,0.19f,0.085f}, {0.61f,0.19f,0.085f}, {0.82f,0.19f,0.085f} }
#include "../_shared/pedal_ui.hpp"
