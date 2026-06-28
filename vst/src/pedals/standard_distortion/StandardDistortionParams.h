#ifndef STANDARD_DISTORTION_PARAMS_H
#define STANDARD_DISTORTION_PARAMS_H

enum StandardDistortionParamId
{
    kDist = 0,
    kTone,
    kLevel,
    kParamCount
};

static const char* const kStandardDistortionNames[kParamCount] = {
    "Dist",
    "Tone",
    "Level",
};

static const char* const kStandardDistortionSymbols[kParamCount] = {
    "dist",
    "tone",
    "level",
};

static const float kStandardDistortionMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kStandardDistortionMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kStandardDistortionDef[kParamCount] = { 0.45f, 0.50f, 0.62f };

#endif // STANDARD_DISTORTION_PARAMS_H
