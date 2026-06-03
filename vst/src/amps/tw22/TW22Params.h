#ifndef TW22_PARAMS_H
#define TW22_PARAMS_H

enum TW22ParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kParamCount
};

static const char* const kTW22Names[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
};

static const char* const kTW22Symbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
};

static const float kTW22Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTW22Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kTW22Def[kParamCount] = { 0.35f, 0.45f, 0.55f, 0.65f };

#endif // TW22_PARAMS_H
