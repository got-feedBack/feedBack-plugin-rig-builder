/* EN30 stompbox UI — shared pedal_ui template. Colour sampled from the
 * Rocksmith art (Amp_EN30); knob count + labels from the plugin params. */
#include "EN30Params.h"
#define PEDAL_TITLE  "BOX DC30"
#define PEDAL_NAMES  kEN30Names
#define PEDAL_DEFS   kEN30Def
#define PEDAL_ACR 95
#define PEDAL_ACG 95
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 560
#define PEDAL_H 300
// AC30C2 controls in a 6-col x 2-row grid (param-id order: NormalVol TBVol
// Treble Bass RevTone RevLevel / Speed Depth ToneCut Master Input Bright).
#define PEDAL_KNOBS { \
  {0.092f,0.30f,0.050f}, {0.255f,0.30f,0.050f}, {0.418f,0.30f,0.050f}, {0.582f,0.30f,0.050f}, {0.745f,0.30f,0.050f}, {0.908f,0.30f,0.050f}, \
  {0.092f,0.74f,0.050f}, {0.255f,0.74f,0.050f}, {0.418f,0.74f,0.050f}, {0.582f,0.74f,0.050f}, {0.745f,0.74f,0.050f}, {0.908f,0.74f,0.050f} }
#include "../../_shared/pedal_ui.hpp"
