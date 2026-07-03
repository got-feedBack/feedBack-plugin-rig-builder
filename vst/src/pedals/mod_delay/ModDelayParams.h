#ifndef MOD_DELAY_PARAMS_H
#define MOD_DELAY_PARAMS_H

enum ModDelayParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kRate,
    kDepth,
    kParamCount
};

static const char* const kModDelayNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
    "Rate",
    "Depth",
};

static const char* const kModDelaySymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
    "rate",
    "depth",
};

static const float kModDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kModDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kModDelayDef[kParamCount] = {
    (360.0f - 20.0f) / (900.0f - 20.0f),
    0.28f,
    0.28f,
    0.4f / 3.5f,
    0.39f,
};

#endif // MOD_DELAY_PARAMS_H
