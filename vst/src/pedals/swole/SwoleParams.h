#ifndef SWOLE_PARAMS_H
#define SWOLE_PARAMS_H

// the game Pedal_Swole exposes Smash and Rate. the game describes it as a
// very heavy saturated compression effect.
enum SwoleParamId
{
    kSmash = 0,
    kRate,
    kParamCount
};

static const char* const kSwoleNames[kParamCount] = {
    "Smash",
    "Rate",
};

static const char* const kSwoleSymbols[kParamCount] = {
    "smash",
    "rate",
};

static const float kSwoleMin[kParamCount] = { 0.0f, 0.0f };
static const float kSwoleMax[kParamCount] = { 1.0f, 1.0f };
static const float kSwoleDef[kParamCount] = { 0.45f, 0.55f };

#endif // SWOLE_PARAMS_H
