#ifndef BZ1_PARAMS_H
#define BZ1_PARAMS_H

enum BZ1ParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kBZ1Names[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kBZ1Symbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kBZ1Min[kParamCount] = { 0.0f, 0.0f };
static const float kBZ1Max[kParamCount] = { 1.0f, 1.0f };
static const float kBZ1Def[kParamCount] = { 0.70f, 0.50f };

#endif // BZ1_PARAMS_H
