#ifndef ACOUSTIC_SIMULATOR_PARAMS_H
#define ACOUSTIC_SIMULATOR_PARAMS_H

enum AcousticSimulatorParamId
{
    kTone = 0,
    kMidShift,
    kBody,
    kMid,
    kParamCount
};

static const char* const kAcousticSimulatorNames[kParamCount] = {
    "Tone",
    "MidShift",
    "Body",
    "Mid",
};

static const char* const kAcousticSimulatorSymbols[kParamCount] = {
    "tone",
    "midshift",
    "body",
    "mid",
};

static const float kAcousticSimulatorMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAcousticSimulatorMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAcousticSimulatorDef[kParamCount] = {
    0.75f,
    (800.0f - 275.0f) / (3000.0f - 275.0f),
    0.40f,
    0.30f,
};

#endif // ACOUSTIC_SIMULATOR_PARAMS_H
