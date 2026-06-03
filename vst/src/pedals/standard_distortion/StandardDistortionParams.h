#ifndef STANDARD_DISTORTION_PARAMS_H
#define STANDARD_DISTORTION_PARAMS_H

enum StandardDistortionParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kStandardDistortionNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kStandardDistortionSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kStandardDistortionMin[kParamCount] = { 0.0f, 0.0f };
static const float kStandardDistortionMax[kParamCount] = { 1.0f, 1.0f };
static const float kStandardDistortionDef[kParamCount] = { 0.45f, 0.50f };

#endif // STANDARD_DISTORTION_PARAMS_H
