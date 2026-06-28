#ifndef VINTAGE_FLANGER_PARAMS_H
#define VINTAGE_FLANGER_PARAMS_H

enum VintageFlangerParamId
{
    kRate = 0,
    kRange,
    kColor,
    kMatrix,
    kParamCount
};

static const char* const kVintageFlangerNames[kParamCount] = {
    "Rate",
    "Range",
    "Color",
    "Matrix",
};

static const char* const kVintageFlangerSymbols[kParamCount] = {
    "rate",
    "range",
    "color",
    "matrix",
};

static const float kVintageFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kVintageFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kVintageFlangerDef[kParamCount] = { 0.25f, 0.46f, 0.22f, 0.0f };

#endif // VINTAGE_FLANGER_PARAMS_H
