#ifndef OCTAVIUS_PARAMS_H
#define OCTAVIUS_PARAMS_H

enum OctaviusParamId
{
    kTone = 0,
    kMix,
    kParamCount
};

static const char* const kOctaviusNames[kParamCount] = {
    "Tone",
    "Mix",
};

static const char* const kOctaviusSymbols[kParamCount] = {
    "tone",
    "mix",
};

static const float kOctaviusMin[kParamCount] = { 0.0f, 0.0f };
static const float kOctaviusMax[kParamCount] = { 1.0f, 1.0f };
static const float kOctaviusDef[kParamCount] = { 0.50f, 0.50f };

#endif // OCTAVIUS_PARAMS_H
