#ifndef BASS_MULTI_COMP_PARAMS_H
#define BASS_MULTI_COMP_PARAMS_H

enum BassMultiCompParamId
{
    kComp = 0,
    kLowTrim,
    kGain,
    kMode,
    kHighTrim,
    kParamCount
};

static const char* const kBassMultiCompNames[kParamCount] = {
    "Comp/Limit",
    "Low Trim",
    "Gain",
    "Mode",
    "High Trim",
};

static const char* const kBassMultiCompSymbols[kParamCount] = {
    "comp",
    "low_trim",
    "gain",
    "mode",
    "high_trim",
};

static const float kBassMultiCompMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassMultiCompMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassMultiCompDef[kParamCount] = { 0.46f, 0.50f, 0.56f, 0.50f, 0.50f };

#endif // BASS_MULTI_COMP_PARAMS_H
