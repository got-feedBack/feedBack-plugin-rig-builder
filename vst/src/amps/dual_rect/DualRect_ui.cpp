/* DualRect stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Amp_CA100); knob count + labels from the plugin params. */
#include "DualRectParams.h"
#define PEDAL_TITLE  "DUAL RECTIFIER"
#define PEDAL_NAMES  kDualRectNames
#define PEDAL_DEFS   kDualRectDef
#define PEDAL_ACR 122
#define PEDAL_ACG 118
#define PEDAL_ACB 122
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 620
#define PEDAL_H 430
// 24 controls in a 6-col x 4-row grid (param-id order: Channel/Output/Rectifier,
// then Green / Orange / Red each Gain/Treble/Mid/Bass/Presence/Master/Mode).
#define PEDAL_KNOBS { \
  {0.10f,0.14f,0.045f}, {0.26f,0.14f,0.045f}, {0.42f,0.14f,0.045f}, {0.58f,0.14f,0.045f}, {0.74f,0.14f,0.045f}, {0.90f,0.14f,0.045f}, \
  {0.10f,0.38f,0.045f}, {0.26f,0.38f,0.045f}, {0.42f,0.38f,0.045f}, {0.58f,0.38f,0.045f}, {0.74f,0.38f,0.045f}, {0.90f,0.38f,0.045f}, \
  {0.10f,0.62f,0.045f}, {0.26f,0.62f,0.045f}, {0.42f,0.62f,0.045f}, {0.58f,0.62f,0.045f}, {0.74f,0.62f,0.045f}, {0.90f,0.62f,0.045f}, \
  {0.10f,0.86f,0.045f}, {0.26f,0.86f,0.045f}, {0.42f,0.86f,0.045f}, {0.58f,0.86f,0.045f}, {0.74f,0.86f,0.045f}, {0.90f,0.86f,0.045f} }
#include "../../_shared/pedal_ui.hpp"
