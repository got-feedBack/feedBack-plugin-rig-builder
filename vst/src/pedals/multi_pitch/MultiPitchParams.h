#ifndef MULTI_PITCH_PARAMS_H
#define MULTI_PITCH_PARAMS_H

enum MultiPitchParamId
{
    kPitch1 = 0,
    kTone,
    kMix,
    kParamCount
};

static const char* const kMultiPitchNames[kParamCount] = {
    "Pitch1",
    "Tone",
    "Mix",
};

static const char* const kMultiPitchSymbols[kParamCount] = {
    "pitch1",
    "tone",
    "mix",
};

static const float kMultiPitchMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kMultiPitchMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kMultiPitchDef[kParamCount] = { 0.25f, 0.50f, 0.50f };

#endif // MULTI_PITCH_PARAMS_H
