#ifndef SUPER_BUZZ_PARAMS_H
#define SUPER_BUZZ_PARAMS_H

enum SuperBuzzParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kSuperBuzzNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kSuperBuzzSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kSuperBuzzMin[kParamCount] = { 0.0f, 0.0f };
static const float kSuperBuzzMax[kParamCount] = { 1.0f, 1.0f };
static const float kSuperBuzzDef[kParamCount] = { 0.62f, 0.58f };

#endif // SUPER_BUZZ_PARAMS_H
