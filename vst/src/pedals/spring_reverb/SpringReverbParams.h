#ifndef SPRING_REVERB_PARAMS_H
#define SPRING_REVERB_PARAMS_H

enum SpringReverbParamId
{
    kTime = 0,
    kMix,
    kDepth,
    kParamCount
};

static const char* const kSpringReverbNames[kParamCount] = {
    "Time",
    "Mix",
    "Depth",
};

static const char* const kSpringReverbSymbols[kParamCount] = {
    "time",
    "mix",
    "depth",
};

static const float kSpringReverbMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kSpringReverbMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kSpringReverbDef[kParamCount] = {
    0.70f,
    0.22f,
    0.70f,
};

#endif // SPRING_REVERB_PARAMS_H
