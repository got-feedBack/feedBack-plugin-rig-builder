/* NYR BS103 stompbox UI — shared pedal_ui template. The face is an original
 * design (no real brand/model). Knob count + labels come from the plugin params;
 * Slopsmith draws its own canvas, so this native UI is only a fallback. */
#include "BassSynthParams.h"
#define PEDAL_TITLE  "BASS SYNTH"
#define PEDAL_NAMES  kBassSynthNames
#define PEDAL_DEFS   kBassSynthDef
#define PEDAL_ACR 40
#define PEDAL_ACG 44
#define PEDAL_ACB 60
#define PEDAL_ARCR 150
#define PEDAL_ARCG 220
#define PEDAL_ARCB 255
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.22f,0.13f,0.066f}, {0.50f,0.13f,0.066f}, {0.78f,0.13f,0.066f}, {0.22f,0.34f,0.066f}, {0.50f,0.34f,0.066f}, {0.78f,0.34f,0.066f}, {0.22f,0.55f,0.066f}, {0.50f,0.55f,0.066f}, {0.78f,0.55f,0.066f} }
#include "../_shared/pedal_ui.hpp"
