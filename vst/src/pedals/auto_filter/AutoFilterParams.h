#ifndef AUTO_FILTER_PARAMS_H
#define AUTO_FILTER_PARAMS_H

enum AutoFilterParamId
{
    kFilterType = 0,
    kRes,
    kSens,
    kAttack,
    kRelease,
    kParamCount
};

static const char* const kAutoFilterNames[kParamCount] = {
    "FilterType",
    "Res",
    "Sens",
    "Attack",
    "Release",
};

static const char* const kAutoFilterSymbols[kParamCount] = {
    "filtertype",
    "res",
    "sens",
    "attack",
    "release",
};

static const float kAutoFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAutoFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAutoFilterDef[kParamCount] = { 0.50f, 0.55f, 0.45f, 0.06f, 0.14f };

#endif // AUTO_FILTER_PARAMS_H
