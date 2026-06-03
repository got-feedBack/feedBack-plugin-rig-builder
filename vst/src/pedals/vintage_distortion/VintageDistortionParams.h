#ifndef VINTAGE_DISTORTION_PARAMS_H
#define VINTAGE_DISTORTION_PARAMS_H

enum VintageDistortionParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kVintageDistortionNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kVintageDistortionSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kVintageDistortionMin[kParamCount] = { 0.0f, 0.0f };
static const float kVintageDistortionMax[kParamCount] = { 1.0f, 1.0f };
static const float kVintageDistortionDef[kParamCount] = { 0.35f, 0.55f };

#endif // VINTAGE_DISTORTION_PARAMS_H
