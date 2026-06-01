#ifndef ENBIGGENATOR_PARAMS_H
#define ENBIGGENATOR_PARAMS_H

// Rocksmith Pedal_Enbiggenator exposes Rate, Depth, and Mix.
enum EnbiggenatorParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kEnbiggenatorNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kEnbiggenatorSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kEnbiggenatorMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kEnbiggenatorMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kEnbiggenatorDef[kParamCount] = { 0.35f, 0.45f, 0.35f };

#endif // ENBIGGENATOR_PARAMS_H
