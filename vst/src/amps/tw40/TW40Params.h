#ifndef TW40_PARAMS_H
#define TW40_PARAMS_H

enum TW40ParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kPres,
    kParamCount
};

static const char* const kTW40Names[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Pres",
};

static const char* const kTW40Symbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "pres",
};

static const float kTW40Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTW40Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kTW40Def[kParamCount] = { 0.42f, 0.48f, 0.55f, 0.62f, 0.56f };

#endif // TW40_PARAMS_H
