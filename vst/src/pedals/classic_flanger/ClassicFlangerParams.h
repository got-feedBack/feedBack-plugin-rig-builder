#ifndef CLASSIC_FLANGER_PARAMS_H
#define CLASSIC_FLANGER_PARAMS_H

enum ClassicFlangerParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kClassicFlangerNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kClassicFlangerSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kClassicFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kClassicFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kClassicFlangerDef[kParamCount] = { 0.26f, 0.44f, 0.18f };

#endif // CLASSIC_FLANGER_PARAMS_H
