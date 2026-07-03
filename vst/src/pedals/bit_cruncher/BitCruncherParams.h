#ifndef BIT_CRUNCHER_PARAMS_H
#define BIT_CRUNCHER_PARAMS_H

// the game Pedal_BitCruncher exposes Attack, FilterType, Mix, Release, Sens.
enum BitCruncherParamId
{
    kAttack = 0,
    kFilterType,
    kMix,
    kRelease,
    kSens,
    kParamCount
};

static const char* const kBitCruncherNames[kParamCount] = {
    "Attack",
    "FilterType",
    "Mix",
    "Release",
    "Sens",
};

static const char* const kBitCruncherSymbols[kParamCount] = {
    "attack",
    "filtertype",
    "mix",
    "release",
    "sens",
};

static const float kBitCruncherMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBitCruncherMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBitCruncherDef[kParamCount] = { 0.34f, 1.0f, 0.68f, 0.10f, 0.25f };

#endif // BIT_CRUNCHER_PARAMS_H
