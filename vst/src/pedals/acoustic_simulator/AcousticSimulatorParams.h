#ifndef ACOUSTIC_SIMULATOR_PARAMS_H
#define ACOUSTIC_SIMULATOR_PARAMS_H

enum AcousticSimulatorParamId
{
    kBrightness = 0,
    kThickness,
    kAmount,
    kVolume,
    kParamCount
};

static const char* const kAcousticSimulatorNames[kParamCount] = {
    "Brightness",
    "Thickness",
    "Amount",
    "Volume",
};

static const char* const kAcousticSimulatorSymbols[kParamCount] = {
    "brightness",
    "thickness",
    "amount",
    "volume",
};

static const float kAcousticSimulatorMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAcousticSimulatorMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAcousticSimulatorDef[kParamCount] = {
    0.50f,
    0.50f,
    0.50f,
    0.62f,
};

#endif // ACOUSTIC_SIMULATOR_PARAMS_H
