#ifndef ANALOG_DELAY_PARAMS_H
#define ANALOG_DELAY_PARAMS_H

enum AnalogDelayParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kAnalogDelayNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kAnalogDelaySymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kAnalogDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kAnalogDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kAnalogDelayDef[kParamCount] = {
    280.0f / 2000.0f,
    0.28f,
    0.22f,
};

#endif // ANALOG_DELAY_PARAMS_H
