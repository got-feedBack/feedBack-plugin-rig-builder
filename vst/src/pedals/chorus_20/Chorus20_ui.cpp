/* Deja Chorus editor. The compatibility-only second speed and selector
 * parameters stay hidden; this version is permanently in Chorus mode. */
#include "Chorus20Params.h"
static const char* const kChorus20UiNames[kParamCount] = {
    "Intensity", "Speed", "Speed2", "SpeedSel", "Volume", "Mode"
};
#define PEDAL_TITLE  "DEJA CHORUS"
#define PEDAL_NAMES  kChorus20UiNames
#define PEDAL_DEFS   kChorus20Def
#define PEDAL_ACR 137
#define PEDAL_ACG 138
#define PEDAL_ACB 144
#define PEDAL_ARCR 225
#define PEDAL_ARCG 230
#define PEDAL_ARCB 238
#define PEDAL_W 360
#define PEDAL_H 440
#define PEDAL_VISIBLE_COUNT 3
#define PEDAL_PARAM_IDS { kSpeed1, kVolume, kIntensity }
#define PEDAL_KNOBS { {0.22f,0.20f,0.095f}, {0.50f,0.20f,0.095f}, {0.78f,0.20f,0.095f} }
#include "../_shared/pedal_ui.hpp"
