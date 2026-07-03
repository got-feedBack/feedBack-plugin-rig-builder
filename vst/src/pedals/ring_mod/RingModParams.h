#ifndef RING_MOD_PARAMS_H
#define RING_MOD_PARAMS_H

enum RingModParamId
{
    kDepth = 0,
    kWaveform,
    kSensitivity,
    kAttack,
    kParamCount
};

static const char* const kRingModNames[kParamCount] = {
    "Depth",
    "Waveform",
    "Sensitivity",
    "Attack",
};

static const char* const kRingModSymbols[kParamCount] = {
    "depth",
    "waveform",
    "sensitivity",
    "attack",
};

static const float kRingModMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kRingModMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kRingModDef[kParamCount] = { 0.35f, 0.0f, 0.45f, 0.35f };

#endif // RING_MOD_PARAMS_H
