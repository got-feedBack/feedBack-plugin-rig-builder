#ifndef LOFI_FILTER_PARAMS_H
#define LOFI_FILTER_PARAMS_H

enum LoFiFilterParamId
{
    kFilterType = 0,
    kMix,
    kParamCount
};

static const char* const kLoFiFilterNames[kParamCount] = {
    "FilterType",
    "Mix",
};

static const char* const kLoFiFilterSymbols[kParamCount] = {
    "filtertype",
    "mix",
};

static const float kLoFiFilterMin[kParamCount] = { 0.0f, 0.0f };
static const float kLoFiFilterMax[kParamCount] = { 1.0f, 1.0f };
static const float kLoFiFilterDef[kParamCount] = {
    0.72f,
    0.38f,
};

#endif // LOFI_FILTER_PARAMS_H
