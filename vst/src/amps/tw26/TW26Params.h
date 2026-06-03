#ifndef TW26_PARAMS_H
#define TW26_PARAMS_H

enum TW26ParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kPres,
    kParamCount
};

static const char* const kTW26Names[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Pres",
};

static const char* const kTW26Symbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "pres",
};

static const float kTW26Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTW26Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kTW26Def[kParamCount] = { 0.38f, 0.55f, 0.58f, 0.60f, 0.50f };

#endif // TW26_PARAMS_H
