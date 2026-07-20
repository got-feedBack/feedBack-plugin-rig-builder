#ifndef RING_MOD_PARAMS_H
#define RING_MOD_PARAMS_H

enum RingModParamId
{
    kPitch = 0,
    kModulation,
    kVolume,
    kPitchRange,
    kModulate,
    kParamCount
};

static const char* const kRingModNames[kParamCount] = {
    "Pitch",
    "Modulation",
    "Volume",
    "Pitch Range",
    "Modulate",
};

static const char* const kRingModSymbols[kParamCount] = {
    "pitch",
    "modulation",
    "volume",
    "pitch_range",
    "modulate",
};

static const float kRingModMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kRingModMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kRingModDef[kParamCount] = { 0.36f, 0.72f, 0.72f, 0.0f, 1.0f };

#endif // RING_MOD_PARAMS_H
