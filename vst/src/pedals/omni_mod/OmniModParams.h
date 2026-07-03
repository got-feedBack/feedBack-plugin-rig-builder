#ifndef OMNI_MOD_PARAMS_H
#define OMNI_MOD_PARAMS_H

enum OmniModParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kOmniModNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kOmniModSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kOmniModMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kOmniModMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kOmniModDef[kParamCount] = { 0.42f, 0.50f, 0.48f };

#endif // OMNI_MOD_PARAMS_H
