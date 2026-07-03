#ifndef MULTI_TREM_PARAMS_H
#define MULTI_TREM_PARAMS_H

enum MultiTremParamId
{
    kRate = 0,
    kDepth,
    kWave,
    kParamCount
};

static const char* const kMultiTremNames[kParamCount] = {
    "Rate",
    "Depth",
    "Wave",
};

static const char* const kMultiTremSymbols[kParamCount] = {
    "rate",
    "depth",
    "wave",
};

static const float kMultiTremMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kMultiTremMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kMultiTremDef[kParamCount] = { 0.62f, 0.76f, 0.75f };

#endif // MULTI_TREM_PARAMS_H
