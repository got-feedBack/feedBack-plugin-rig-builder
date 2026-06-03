#ifndef LIMITER_PARAMS_H
#define LIMITER_PARAMS_H

enum LimiterParamId
{
    kLimit = 0,
    kRate,
    kParamCount
};

static const char* const kLimiterNames[kParamCount] = {
    "Limit",
    "Rate",
};

static const char* const kLimiterSymbols[kParamCount] = {
    "limit",
    "rate",
};

static const float kLimiterMin[kParamCount] = { 0.0f, 0.0f };
static const float kLimiterMax[kParamCount] = { 1.0f, 1.0f };
static const float kLimiterDef[kParamCount] = { 0.28f, 0.45f };

#endif // LIMITER_PARAMS_H
