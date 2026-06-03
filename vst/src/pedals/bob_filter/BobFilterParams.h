#ifndef BOB_FILTER_PARAMS_H
#define BOB_FILTER_PARAMS_H

enum BobFilterParamId
{
    kSens = 0,
    kAttack,
    kRelease,
    kMix,
    kFilter,
    kParamCount
};

static const char* const kBobFilterNames[kParamCount] = {
    "Sens",
    "Attack",
    "Release",
    "Mix",
    "Filter",
};

static const char* const kBobFilterSymbols[kParamCount] = {
    "sens",
    "attack",
    "release",
    "mix",
    "filter",
};

static const float kBobFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBobFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBobFilterDef[kParamCount] = {
    0.75f,
    (55.0f - 1.0f) / (250.0f - 1.0f),
    (180.0f - 10.0f) / (1000.0f - 10.0f),
    0.80f,
    1.0f,
};

#endif // BOB_FILTER_PARAMS_H
