#ifndef ALLOY_DISTORTION_PARAMS_H
#define ALLOY_DISTORTION_PARAMS_H

enum AlloyDistortionParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kAlloyDistortionNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kAlloyDistortionSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kAlloyDistortionMin[kParamCount] = { 0.0f, 0.0f };
static const float kAlloyDistortionMax[kParamCount] = { 1.0f, 1.0f };
static const float kAlloyDistortionDef[kParamCount] = { 0.55f, 0.65f };

#endif // ALLOY_DISTORTION_PARAMS_H
