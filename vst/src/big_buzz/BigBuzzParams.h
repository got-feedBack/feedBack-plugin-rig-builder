#ifndef BUZZ_TOO_PARAMS_H
#define BUZZ_TOO_PARAMS_H

enum BigBuzzParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kBigBuzzNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kBigBuzzSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kBigBuzzMin[kParamCount] = { 0.0f, 0.0f };
static const float kBigBuzzMax[kParamCount] = { 1.0f, 1.0f };
static const float kBigBuzzDef[kParamCount] = { 0.64f, 0.46f };

#endif // BUZZ_TOO_PARAMS_H
