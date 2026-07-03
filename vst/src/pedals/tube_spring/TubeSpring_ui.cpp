/* TubeSpring stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_TubeSpring); knob count + labels from the plugin params. */
#include "TubeSpringParams.h"
#define PEDAL_TITLE  "TUBE SPRING"
#define PEDAL_NAMES  kTubeSpringNames
#define PEDAL_DEFS   kTubeSpringDef
#define PEDAL_ACR 111
#define PEDAL_ACG 152
#define PEDAL_ACB 62
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
