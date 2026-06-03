#ifndef ANALOG_CHORUS_PARAMS_H
#define ANALOG_CHORUS_PARAMS_H

enum AnalogChorusParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kAnalogChorusNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kAnalogChorusSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kAnalogChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kAnalogChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kAnalogChorusDef[kParamCount] = { 0.15f, 0.55f, 0.45f };

#endif // ANALOG_CHORUS_PARAMS_H
