#ifndef NPN_DELAY_PARAMS_H
#define NPN_DELAY_PARAMS_H

enum NpnDelayParamId
{
    kRepeatRate = 0,
    kEcho,
    kIntensity,
    kParamCount
};

static const char* const kNpnDelayNames[kParamCount] = {
    "Repeat Rate",
    "Echo",
    "Intensity",
};

static const char* const kNpnDelaySymbols[kParamCount] = {
    "repeat_rate",
    "echo",
    "intensity",
};

static const float kNpnDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kNpnDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kNpnDelayDef[kParamCount] = {
    1.0f - (220.0f - 20.0f) / (300.0f - 20.0f),
    0.24f,
    0.30f,
};

#endif // NPN_DELAY_PARAMS_H
