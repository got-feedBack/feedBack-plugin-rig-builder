#ifndef CHORUS20_PARAMS_H
#define CHORUS20_PARAMS_H

enum Chorus20ParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kChorus20Names[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kChorus20Symbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kChorus20Min[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kChorus20Max[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kChorus20Def[kParamCount] = { 0.18f, 0.60f, 0.42f };

#endif // CHORUS20_PARAMS_H
