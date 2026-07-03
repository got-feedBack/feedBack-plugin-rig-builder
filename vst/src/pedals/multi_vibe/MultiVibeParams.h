#ifndef MULTI_VIBE_PARAMS_H
#define MULTI_VIBE_PARAMS_H

enum MultiVibeParamId
{
    kSpeed = 0,
    kMix,
    kWaveform,
    kParamCount
};

static const char* const kMultiVibeNames[kParamCount] = {
    "Speed",
    "Mix",
    "Waveform",
};

static const char* const kMultiVibeSymbols[kParamCount] = {
    "speed",
    "mix",
    "waveform",
};

static const float kMultiVibeMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kMultiVibeMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kMultiVibeDef[kParamCount] = { 0.52f, 0.60f, 1.0f };

#endif // MULTI_VIBE_PARAMS_H
