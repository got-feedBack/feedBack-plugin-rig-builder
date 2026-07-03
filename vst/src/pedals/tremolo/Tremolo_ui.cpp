/* Tremolo stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Pedal_Tremolo); knob count + labels from the plugin params. */
#include "TremoloParams.h"
#define PEDAL_TITLE  "TREMOLO"
#define PEDAL_NAMES  kTremoloNames
#define PEDAL_DEFS   kTremoloDef
#define PEDAL_ACR 182
#define PEDAL_ACG 182
#define PEDAL_ACB 51
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
