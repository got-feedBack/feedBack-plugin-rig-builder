#ifndef VINTAGE_FLANGER_PARAMS_H
#define VINTAGE_FLANGER_PARAMS_H

enum VintageFlangerParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kVintageFlangerNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kVintageFlangerSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kVintageFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kVintageFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kVintageFlangerDef[kParamCount] = { 0.25f, 0.45f, 0.18f };

#endif // VINTAGE_FLANGER_PARAMS_H
