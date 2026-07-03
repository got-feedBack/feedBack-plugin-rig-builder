#ifndef BAKED_ROTATOE_PARAMS_H
#define BAKED_ROTATOE_PARAMS_H

enum BakedRotatoeParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kBalance,
    kParamCount
};

static const char* const kBakedRotatoeNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
    "Balance",
};

static const char* const kBakedRotatoeSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
    "balance",
};

static const float kBakedRotatoeMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBakedRotatoeMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBakedRotatoeDef[kParamCount] = { 0.78f, 0.74f, 0.48f, 0.55f };

#endif // BAKED_ROTATOE_PARAMS_H
