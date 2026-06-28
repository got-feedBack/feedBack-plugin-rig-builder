/* Chorus20 stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_Chorus20); knob count + labels from the plugin params. */
#include "Chorus20Params.h"
#define PEDAL_TITLE  "DEJA CHORUS"
#define PEDAL_NAMES  kChorus20Names
#define PEDAL_DEFS   kChorus20Def
#define PEDAL_ACR 137
#define PEDAL_ACG 138
#define PEDAL_ACB 144
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.21f,0.15f,0.075f}, {0.50f,0.15f,0.075f}, {0.79f,0.15f,0.075f}, {0.30f,0.35f,0.070f}, {0.52f,0.35f,0.082f}, {0.74f,0.35f,0.070f} }
#include "../_shared/pedal_ui.hpp"
