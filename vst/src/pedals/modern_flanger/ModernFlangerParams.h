#ifndef MODERN_FLANGER_PARAMS_H
#define MODERN_FLANGER_PARAMS_H

enum ModernFlangerParamId
{
    kRate = 0,
    kDepth,
    kRegen,
    kMix,
    kParamCount
};

static const char* const kModernFlangerNames[kParamCount] = {
    "Rate",
    "Depth",
    "Regen",
    "Mix",
};

static const char* const kModernFlangerSymbols[kParamCount] = {
    "rate",
    "depth",
    "regen",
    "mix",
};

static const float kModernFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kModernFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kModernFlangerDef[kParamCount] = { 0.26f, 0.48f, 0.15f, 0.48f };

#endif // MODERN_FLANGER_PARAMS_H
