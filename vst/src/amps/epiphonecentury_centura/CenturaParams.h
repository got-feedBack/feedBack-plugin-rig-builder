#ifndef CENTURA_PARAMS_H
#define CENTURA_PARAMS_H

// "Epicall Centura" — Epiphone Electar Century (75th reissue) front panel, 1:1:
//   Volume : the single Volume control (cranks the 2x 6V6 power amp; non-master).
//   Tone   : the passive tone control (down = dark, up = bright).
//   Voice  : the input voicing — Dark / Normal / Bright (the panel's three input
//            jacks); low = dark, high = bright.
//   Boost  : the Volume pot's PULL BOOST (extra preamp gain).  Power/Speaker/
//            Footswitch are cosmetic on the canvas face.
enum CenturaParamId {
    kVolume = 0, kTone, kVoice,   // knobs
    kBoost,                        // switch (pull boost)
    kParamCount
};

static const char* const kCenturaNames[kParamCount] = {
    "Volume", "Tone", "Voice", "Boost"
};
static const char* const kCenturaSymbols[kParamCount] = {
    "volume", "tone", "voice", "boost"
};
static const float kCenturaMin[kParamCount] = { 0,0,0, 0 };
static const float kCenturaMax[kParamCount] = { 1,1,1, 1 };
// Volume 0.5; Tone 0.5; Voice 0.6 (Normal/slightly bright); Boost off.
static const float kCenturaDef[kParamCount] = {
    0.50f, 0.50f, 0.60f, 0.00f
};

#endif // CENTURA_PARAMS_H
