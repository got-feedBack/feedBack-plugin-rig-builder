#ifndef ANALOG_DELAY_PARAMS_H
#define ANALOG_DELAY_PARAMS_H

enum AnalogDelayParamId
{
    kDrive = 0,
    kOutput,
    kTime,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kAnalogDelayNames[kParamCount] = {
    "Drive",
    "Output",
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kAnalogDelaySymbols[kParamCount] = {
    "drive",
    "output",
    "time",
    "feedback",
    "mix",
};

static const float kAnalogDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAnalogDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAnalogDelayDef[kParamCount] = {
    0.36f,
    0.68f,
    (360.0f - 40.0f) / (1000.0f - 40.0f),
    0.28f,
    0.32f,
};

#endif // ANALOG_DELAY_PARAMS_H
