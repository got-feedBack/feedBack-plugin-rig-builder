#ifndef RANGE_BOOSTER_PARAMS_H
#define RANGE_BOOSTER_PARAMS_H

enum RangeBoosterParamId
{
    kBoost = 0,
    kParamCount
};

static const char* const kRangeBoosterNames[kParamCount] = {
    "Boost",
};

static const char* const kRangeBoosterSymbols[kParamCount] = {
    "boost",
};

static const float kRangeBoosterMin[kParamCount] = { 0.0f };
static const float kRangeBoosterMax[kParamCount] = { 1.0f };
static const float kRangeBoosterDef[kParamCount] = { 0.45f };

#endif // RANGE_BOOSTER_PARAMS_H
