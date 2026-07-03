#ifndef CITRUS_PARAMS_H
#define CITRUS_PARAMS_H

// "Citrus AD200" — Orange AD200B (MK3) all-tube bass head front panel, 1:1:
//   Gain   : 12AX7 input drive (clean below ~1/3, grinds when cranked).
//   Bass / Middle / Treble : the Orange PASSIVE SUBTRACTIVE tone stack — full up
//            (1.0) = factory-flat; turning down CUTS that band (per the manual).
//   Master : overall level into the 4x KT88 push-pull (~200 W, huge clean
//            headroom; creamy power-tube overdrive when pushed).
//   Active : the Active input (pads hot active basses; Passive = full gain).
enum CitrusParamId {
    kGain = 0, kBass, kMiddle, kTreble, kMaster,   // knobs
    kActive,                                        // switch (Active input)
    kParamCount
};

static const char* const kCitrusNames[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Master", "Active"
};
static const char* const kCitrusSymbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "master", "active"
};
static const float kCitrusMin[kParamCount] = { 0,0,0,0,0, 0 };
static const float kCitrusMax[kParamCount] = { 1,1,1,1,1, 1 };
// Gain 0.4 (clean-ish); tone stack 1.0 = FLAT (subtractive, full up); Master 0.7;
// Active off (Passive input).
static const float kCitrusDef[kParamCount] = {
    0.40f, 1.00f, 1.00f, 1.00f, 0.70f, 0.00f
};

#endif // CITRUS_PARAMS_H
