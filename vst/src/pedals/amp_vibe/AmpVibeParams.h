#ifndef AMP_VIBE_PARAMS_H
#define AMP_VIBE_PARAMS_H

enum AmpVibeParamId
{
    kSpeed = 0,
    kIntensity,
    kVolume,
    kMode,
    kParamCount
};

static const char* const kAmpVibeNames[kParamCount] = {
    "Speed",
    "Intensity",
    "Volume",
    "Mode",
};

static const char* const kAmpVibeSymbols[kParamCount] = {
    "speed",
    "intensity",
    "volume",
    "mode",
};

static const float kAmpVibeMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAmpVibeMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAmpVibeDef[kParamCount] = {
    0.49f,
    0.62f,
    0.62f,
    0.0f,
};

#endif // AMP_VIBE_PARAMS_H
