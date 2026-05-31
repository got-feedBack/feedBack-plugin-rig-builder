#ifndef CHORUS_PARAMS_H
#define CHORUS_PARAMS_H

enum ChorusParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kChorusNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kChorusSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kChorusDef[kParamCount] = { 0.28f, 0.48f, 0.62f };

#endif // CHORUS_PARAMS_H
