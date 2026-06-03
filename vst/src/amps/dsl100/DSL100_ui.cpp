/* DSL100 stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_MarshallDSL100H); knob count + labels from the plugin params. */
#include "DSL100Params.h"
#define PEDAL_TITLE  "DSL100"
#define PEDAL_NAMES  kDSL100Names
#define PEDAL_DEFS   kDSL100Def
#define PEDAL_ACR 117
#define PEDAL_ACG 99
#define PEDAL_ACB 60
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.15f,0.078f}, {0.50f,0.15f,0.078f}, {0.78f,0.15f,0.078f}, {0.22f,0.36f,0.078f}, {0.50f,0.36f,0.078f}, {0.78f,0.36f,0.078f} }
#include "../_shared/pedal_ui.hpp"
