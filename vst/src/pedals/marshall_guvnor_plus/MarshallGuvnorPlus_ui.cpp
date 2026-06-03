/* MarshallGuvnorPlus stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Pedal_MarshallGuvnorPlus); knob count + labels from the plugin params. */
#include "MarshallGuvnorPlusParams.h"
#define PEDAL_TITLE  "MARSHALL GUVNOR PLUS"
#define PEDAL_NAMES  kMarshallGuvnorPlusNames
#define PEDAL_DEFS   kMarshallGuvnorPlusDef
#define PEDAL_ACR 153
#define PEDAL_ACG 139
#define PEDAL_ACB 105
#define PEDAL_ARCR 40
#define PEDAL_ARCG 40
#define PEDAL_ARCB 46
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_KNOBS { {0.25f,0.15f,0.082f}, {0.50f,0.15f,0.082f}, {0.75f,0.15f,0.082f}, {0.36f,0.36f,0.082f}, {0.64f,0.36f,0.082f} }
#include "../_shared/pedal_ui.hpp"
