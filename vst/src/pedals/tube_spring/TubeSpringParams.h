#ifndef TUBE_SPRING_PARAMS_H
#define TUBE_SPRING_PARAMS_H

enum TubeSpringParamId
{
    kMix = 0,
    kDepth,
    kParamCount
};

static const char* const kTubeSpringNames[kParamCount] = {
    "Mix",
    "Depth",
};

static const char* const kTubeSpringSymbols[kParamCount] = {
    "mix",
    "depth",
};

static const float kTubeSpringMin[kParamCount] = { 0.0f, 0.0f };
static const float kTubeSpringMax[kParamCount] = { 1.0f, 1.0f };
static const float kTubeSpringDef[kParamCount] = { 0.30f, 0.55f };

#endif // TUBE_SPRING_PARAMS_H
