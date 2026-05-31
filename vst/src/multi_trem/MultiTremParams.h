#ifndef MULTI_TREM_PARAMS_H
#define MULTI_TREM_PARAMS_H

enum MultiTremParamId
{
    kSpeed = 0,
    kMix,
    kWaveform,
    kParamCount
};

static const char* const kMultiTremNames[kParamCount] = {
    "Speed",
    "Mix",
    "Waveform",
};

static const char* const kMultiTremSymbols[kParamCount] = {
    "speed",
    "mix",
    "waveform",
};

static const float kMultiTremMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kMultiTremMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kMultiTremDef[kParamCount] = { 0.62f, 0.76f, 0.75f };

#endif // MULTI_TREM_PARAMS_H
