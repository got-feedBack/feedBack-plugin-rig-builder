/* TW22 stompbox UI — shared pedal_ui template. Colour sampled from the
 * the game art (Amp_TW22); knob count + labels from the plugin params. */
#include "TW22Params.h"
#define PEDAL_TITLE  "Bender SuperNova 22"
#define PEDAL_NAMES  kTW22Names
#define PEDAL_DEFS   kTW22Def
#define PEDAL_ACR 91
#define PEDAL_ACG 92
#define PEDAL_ACB 105
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 600
#define PEDAL_H 320
// Full Super-Sonic 22 panel: 13 controls in a 7-col x 2-row grid (param-id order:
// VintVol VintTreble VintBass NormFat Channel Gain1 Gain2 / BurnTreble BurnBass
// BurnMid BurnVol Reverb Presence).
#define PEDAL_KNOBS { \
  {0.08f,0.30f,0.046f}, {0.215f,0.30f,0.046f}, {0.35f,0.30f,0.046f}, {0.485f,0.30f,0.046f}, {0.62f,0.30f,0.046f}, {0.755f,0.30f,0.046f}, {0.89f,0.30f,0.046f}, \
  {0.08f,0.74f,0.046f}, {0.215f,0.74f,0.046f}, {0.35f,0.74f,0.046f}, {0.485f,0.74f,0.046f}, {0.62f,0.74f,0.046f}, {0.755f,0.74f,0.046f} }
#include "../../_shared/pedal_ui.hpp"
