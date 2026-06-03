/* PlanePhase stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_PlanePhase); knob count + labels from the plugin params. */
#include "PlanePhaseParams.h"
#define PEDAL_TITLE  "PLANE PHASE"
#define PEDAL_NAMES  kPlanePhaseNames
#define PEDAL_DEFS   kPlanePhaseDef
#define PEDAL_ACR 125
#define PEDAL_ACG 155
#define PEDAL_ACB 128
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.20f,0.100f}, {0.50f,0.20f,0.100f}, {0.78f,0.20f,0.100f} }
#include "../_shared/pedal_ui.hpp"
