#ifndef LOFI_FILTER_PARAMS_H
#define LOFI_FILTER_PARAMS_H

enum LoFiFilterParamId
{
    kDrive = 0,
    kLevel,
    kLo,
    kHi,
    kParamCount
};

static const char* const kLoFiFilterNames[kParamCount] = {
    "Drive",
    "Level",
    "Lo",
    "Hi",
};

static const char* const kLoFiFilterSymbols[kParamCount] = {
    "drive",
    "level",
    "lo",
    "hi",
};

static const float kLoFiFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kLoFiFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kLoFiFilterDef[kParamCount] = {
    0.36f,
    0.66f,
    0.20f,
    0.72f,
};

#endif // LOFI_FILTER_PARAMS_H
