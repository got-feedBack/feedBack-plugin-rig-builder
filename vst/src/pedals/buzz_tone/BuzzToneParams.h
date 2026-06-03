#ifndef BUZZ_TONE_PARAMS_H
#define BUZZ_TONE_PARAMS_H

enum BuzzToneParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kBuzzToneNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kBuzzToneSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kBuzzToneMin[kParamCount] = { 0.0f, 0.0f };
static const float kBuzzToneMax[kParamCount] = { 1.0f, 1.0f };
static const float kBuzzToneDef[kParamCount] = { 0.78f, 0.62f };

#endif // BUZZ_TONE_PARAMS_H
