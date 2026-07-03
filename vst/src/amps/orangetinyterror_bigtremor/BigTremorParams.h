#ifndef BIGTREMOR_PARAMS_H
#define BIGTREMOR_PARAMS_H

// "Citrus Big Tremor" — Orange Tiny Terror single-channel head front panel, 1:1:
//   Volume : master level into the 2x EL84 cathode-biased power amp.
//   Tone   : the single passive tone control (down = dark / treble cut, up = bright).
//   Gain   : preamp drive across the two 12AX7 stages (clean low, crunchy cranked).
//   Half   : OUTPUT switch — 15 W (full, both EL84) vs 7 W (lower headroom, earlier
//            breakup). Panel also has Power/Standby (cosmetic on the canvas face).
enum BigTremorParamId {
    kVolume = 0, kTone, kGain,   // knobs (panel order: Volume · Tone · Gain)
    kHalf,                        // switch (7 W half-power)
    kCabSim,                      // fallback 1x12/2x12 speaker voice
    kParamCount
};

static const char* const kBigTremorNames[kParamCount] = {
    "Volume", "Tone", "Gain", "Half Power", "Cab Sim"
};
static const char* const kBigTremorSymbols[kParamCount] = {
    "volume", "tone", "gain", "half", "cabsim"
};
static const float kBigTremorMin[kParamCount] = { 0,0,0, 0,0 };
static const float kBigTremorMax[kParamCount] = { 1,1,1, 1,1 };
// Volume 0.6; Tone 0.6 (slightly bright); Gain 0.5 (edge of breakup); 15 W (Half off).
static const float kBigTremorDef[kParamCount] = {
    0.60f, 0.60f, 0.50f, 0.00f, 1.00f
};

#endif // BIGTREMOR_PARAMS_H
