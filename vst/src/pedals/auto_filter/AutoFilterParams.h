#ifndef AUTO_FILTER_PARAMS_H
#define AUTO_FILTER_PARAMS_H

enum AutoFilterParamId
{
    kGain = 0,
    kPeak,
    kMode,
    kRange,
    kDirection,
    kParamCount
};

static const char* const kAutoFilterNames[kParamCount] = {
    "Gain",
    "Peak",
    "Mode",
    "Range",
    "Direction",
};

static const char* const kAutoFilterSymbols[kParamCount] = {
    "gain",
    "peak",
    "mode",
    "range",
    "direction",
};

static const float kAutoFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAutoFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAutoFilterDef[kParamCount] = { 0.46f, 0.56f, 0.50f, 1.0f, 1.0f };

#endif // AUTO_FILTER_PARAMS_H
