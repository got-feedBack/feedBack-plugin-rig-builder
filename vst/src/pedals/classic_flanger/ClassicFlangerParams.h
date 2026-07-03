#ifndef CLASSIC_FLANGER_PARAMS_H
#define CLASSIC_FLANGER_PARAMS_H

enum ClassicFlangerParamId
{
    kManual = 0,
    kDepth,
    kRate,
    kRes,
    kParamCount
};

static const char* const kClassicFlangerNames[kParamCount] = {
    "Manual",
    "Depth",
    "Rate",
    "Res",
};

static const char* const kClassicFlangerSymbols[kParamCount] = {
    "manual",
    "depth",
    "rate",
    "res",
};

static const float kClassicFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kClassicFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kClassicFlangerDef[kParamCount] = { 0.42f, 0.44f, 0.26f, 0.24f };

#endif // CLASSIC_FLANGER_PARAMS_H
