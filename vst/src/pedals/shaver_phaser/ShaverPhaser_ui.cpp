/* ShaverPhaser stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_ShaverPhaser); knob count + labels from the plugin params. */
#include "ShaverPhaserParams.h"
#define PEDAL_TITLE  "SHAVER PHASER"
#define PEDAL_NAMES  kShaverPhaserNames
#define PEDAL_DEFS   kShaverPhaserDef
#define PEDAL_ACR 78
#define PEDAL_ACG 75
#define PEDAL_ACB 104
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.32f,0.20f,0.110f}, {0.68f,0.20f,0.110f} }
#include "../_shared/pedal_ui.hpp"
