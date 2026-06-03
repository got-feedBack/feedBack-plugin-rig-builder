#ifndef AUTO_VIBE_PARAMS_H
#define AUTO_VIBE_PARAMS_H

// Rocksmith Pedal_AutoVibe exposes Sens, Attack, Release, and Mix.
enum AutoVibeParamId
{
    kSens = 0,
    kAttack,
    kRelease,
    kMix,
    kParamCount
};

static const char* const kAutoVibeNames[kParamCount] = {
    "Sens",
    "Attack",
    "Release",
    "Mix",
};

static const char* const kAutoVibeSymbols[kParamCount] = {
    "sens",
    "attack",
    "release",
    "mix",
};

static const float kAutoVibeMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAutoVibeMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAutoVibeDef[kParamCount] = { 0.55f, 0.12f, 0.28f, 0.55f };

#endif // AUTO_VIBE_PARAMS_H
