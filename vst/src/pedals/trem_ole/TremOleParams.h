#ifndef TREM_OLE_PARAMS_H
#define TREM_OLE_PARAMS_H

// the game Pedal_TremOle exposes Sens, Attack, Release, and Mix.
enum TremOleParamId
{
    kSens = 0,
    kAttack,
    kRelease,
    kMix,
    kParamCount
};

static const char* const kTremOleNames[kParamCount] = {
    "Sens",
    "Attack",
    "Release",
    "Mix",
};

static const char* const kTremOleSymbols[kParamCount] = {
    "sens",
    "attack",
    "release",
    "mix",
};

static const float kTremOleMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTremOleMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kTremOleDef[kParamCount] = { 0.55f, 0.10f, 0.24f, 0.62f };

#endif // TREM_OLE_PARAMS_H
