#ifndef BASS_MULTI_COMP_PARAMS_H
#define BASS_MULTI_COMP_PARAMS_H

enum BassMultiCompParamId
{
    kCompress = 0,
    kFilter,
    kRate,
    kParamCount
};

static const char* const kBassMultiCompNames[kParamCount] = {
    "Compress",
    "Filter",
    "Rate",
};

static const char* const kBassMultiCompSymbols[kParamCount] = {
    "compress",
    "filter",
    "rate",
};

static const float kBassMultiCompMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kBassMultiCompMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kBassMultiCompDef[kParamCount] = {
    0.50f,
    (600.0f - 110.0f) / (1000.0f - 110.0f),
    0.40f,
};

#endif // BASS_MULTI_COMP_PARAMS_H
