#ifndef DSL100_PARAMS_H
#define DSL100_PARAMS_H

enum DSL100ParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kPres,
    kRes,
    kParamCount
};

static const char* const kDSL100Names[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Pres",
    "Res",
};

static const char* const kDSL100Symbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "pres",
    "res",
};

static const float kDSL100Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kDSL100Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDSL100Def[kParamCount] = { 0.55f, 0.58f, 0.55f, 0.62f, 0.45f, 0.50f };

#endif // DSL100_PARAMS_H
