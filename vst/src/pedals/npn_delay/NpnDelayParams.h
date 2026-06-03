#ifndef NPN_DELAY_PARAMS_H
#define NPN_DELAY_PARAMS_H

enum NpnDelayParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kNpnDelayNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kNpnDelaySymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kNpnDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kNpnDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kNpnDelayDef[kParamCount] = {
    220.0f / 2000.0f,
    0.30f,
    0.24f,
};

#endif // NPN_DELAY_PARAMS_H
