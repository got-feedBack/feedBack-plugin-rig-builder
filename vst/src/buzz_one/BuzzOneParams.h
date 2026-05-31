#ifndef BUZZ_ONE_PARAMS_H
#define BUZZ_ONE_PARAMS_H

enum BuzzOneParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kBuzzOneNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kBuzzOneSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kBuzzOneMin[kParamCount] = { 0.0f, 0.0f };
static const float kBuzzOneMax[kParamCount] = { 1.0f, 1.0f };
static const float kBuzzOneDef[kParamCount] = { 0.62f, 0.58f };

#endif // BUZZ_ONE_PARAMS_H
