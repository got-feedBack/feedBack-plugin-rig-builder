#ifndef OMNI_MOD_PARAMS_H
#define OMNI_MOD_PARAMS_H

enum OmniModParamId
{
    kRate = 0,
    kIntensity,
    kVolume,
    kMode,
    kParamCount
};

static const char* const kOmniModNames[kParamCount] = {
    "Speed",
    "Intensity",
    "Volume",
    "Mode",
};

static const char* const kOmniModSymbols[kParamCount] = {
    "speed",
    "intensity",
    "volume",
    "mode",
};

static const float kOmniModMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kOmniModMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kOmniModDef[kParamCount] = { 0.34f, 0.58f, 0.62f, 0.0f };

#endif // OMNI_MOD_PARAMS_H
