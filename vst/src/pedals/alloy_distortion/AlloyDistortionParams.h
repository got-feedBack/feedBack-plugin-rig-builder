#ifndef ALLOY_DISTORTION_PARAMS_H
#define ALLOY_DISTORTION_PARAMS_H

enum AlloyDistortionParamId
{
    kDist = 0,
    kColorLow,
    kColorHigh,
    kLevel,
    kParamCount
};

static const char* const kAlloyDistortionNames[kParamCount] = {
    "Dist",
    "ColorLow",
    "ColorHigh",
    "Level",
};

static const char* const kAlloyDistortionSymbols[kParamCount] = {
    "dist",
    "color_low",
    "color_high",
    "level",
};

static const float kAlloyDistortionMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAlloyDistortionMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAlloyDistortionDef[kParamCount] = { 0.55f, 0.82f, 0.72f, 0.62f };

#endif // ALLOY_DISTORTION_PARAMS_H
