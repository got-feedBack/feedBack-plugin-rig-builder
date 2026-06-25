#ifndef SUPER_BUZZ_PARAMS_H
#define SUPER_BUZZ_PARAMS_H

enum SuperBuzzParamId
{
    kExpander = 0,
    kToneSwitch,
    kBalance,
    kParamCount
};

static const char* const kSuperBuzzNames[kParamCount] = {
    "Expander",
    "Tone SW",
    "Balance",
};

static const char* const kSuperBuzzSymbols[kParamCount] = {
    "expander",
    "tone_sw",
    "balance",
};

static const float kSuperBuzzMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kSuperBuzzMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kSuperBuzzDef[kParamCount] = { 0.62f, 1.0f, 0.62f };

#endif // SUPER_BUZZ_PARAMS_H
