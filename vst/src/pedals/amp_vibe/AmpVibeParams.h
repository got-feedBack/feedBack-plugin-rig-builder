#ifndef AMP_VIBE_PARAMS_H
#define AMP_VIBE_PARAMS_H

enum AmpVibeParamId
{
    kSpeed = 0,
    kMix,
    kParamCount
};

static const char* const kAmpVibeNames[kParamCount] = {
    "Speed",
    "Mix",
};

static const char* const kAmpVibeSymbols[kParamCount] = {
    "speed",
    "mix",
};

static const float kAmpVibeMin[kParamCount] = { 0.0f, 0.0f };
static const float kAmpVibeMax[kParamCount] = { 1.0f, 1.0f };
static const float kAmpVibeDef[kParamCount] = {
    0.52f,
    0.48f,
};

#endif // AMP_VIBE_PARAMS_H
