#ifndef EN30_PARAMS_H
#define EN30_PARAMS_H

enum EN30ParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kPres,
    kBright,
    kParamCount
};

static const char* const kEN30Names[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Pres",
    "Bright",
};

static const char* const kEN30Symbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "pres",
    "bright",
};

static const float kEN30Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kEN30Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kEN30Def[kParamCount] = { 0.38f, 0.42f, 0.55f, 0.72f, 0.55f, 1.0f };

#endif // EN30_PARAMS_H
