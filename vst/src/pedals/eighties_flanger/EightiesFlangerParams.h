#ifndef EIGHTIES_FLANGER_PARAMS_H
#define EIGHTIES_FLANGER_PARAMS_H

enum EightiesFlangerParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kEightiesFlangerNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kEightiesFlangerSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kEightiesFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kEightiesFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kEightiesFlangerDef[kParamCount] = { 0.22f, 0.50f, 0.16f };

#endif // EIGHTIES_FLANGER_PARAMS_H
