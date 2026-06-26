#ifndef LIMITER_PARAMS_H
#define LIMITER_PARAMS_H

enum LimiterParamId
{
    kLevel = 0,
    kTone,
    kRelease,
    kThreshold,
    kParamCount
};

static const char* const kLimiterNames[kParamCount] = {
    "Level",
    "Tone",
    "Release",
    "Threshold",
};

static const char* const kLimiterSymbols[kParamCount] = {
    "level",
    "tone",
    "release",
    "threshold",
};

static const float kLimiterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kLimiterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kLimiterDef[kParamCount] = { 0.58f, 0.52f, 0.34f, 0.42f };

#endif // LIMITER_PARAMS_H
