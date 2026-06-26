#ifndef BOB_FILTER_PARAMS_H
#define BOB_FILTER_PARAMS_H

enum BobFilterParamId
{
    kDrive = 0,
    kOutput,
    kPattern,
    kRate,
    kEnvelope,
    kMix,
    kMode,
    kParamCount
};

static const char* const kBobFilterNames[kParamCount] = {
    "Drive",
    "Output",
    "Pattern",
    "Rate",
    "Envelope",
    "Mix",
    "Mode",
};

static const char* const kBobFilterSymbols[kParamCount] = {
    "drive",
    "output",
    "pattern",
    "rate",
    "envelope",
    "mix",
    "mode",
};

static const float kBobFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBobFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBobFilterDef[kParamCount] = {
    0.48f,
    0.68f,
    0.28f,
    0.34f,
    0.62f,
    0.78f,
    1.0f,
};

#endif // BOB_FILTER_PARAMS_H
