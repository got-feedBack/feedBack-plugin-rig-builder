#ifndef ACOUSTIC_SIMULATOR_PARAMS_H
#define ACOUSTIC_SIMULATOR_PARAMS_H

enum AcousticSimulatorParamId
{
    kGain = 0,
    kTop,
    kBody,
    kVolume,
    kParamCount
};

static const char* const kAcousticSimulatorNames[kParamCount] = {
    "Gain",
    "Top",
    "Body",
    "Volume",
};

static const char* const kAcousticSimulatorSymbols[kParamCount] = {
    "gain",
    "top",
    "body",
    "volume",
};

static const float kAcousticSimulatorMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAcousticSimulatorMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAcousticSimulatorDef[kParamCount] = {
    0.44f,
    0.68f,
    0.48f,
    0.62f,
};

#endif // ACOUSTIC_SIMULATOR_PARAMS_H
