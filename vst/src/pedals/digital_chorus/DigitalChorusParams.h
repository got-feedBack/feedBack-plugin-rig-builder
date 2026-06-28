#ifndef DIGITAL_CHORUS_PARAMS_H
#define DIGITAL_CHORUS_PARAMS_H

enum DigitalChorusParamId
{
    kLevel = 0,
    kRate,
    kDepth,
    kLoFilter,
    kHiFilter,
    kParamCount
};

static const char* const kDigitalChorusNames[kParamCount] = {
    "E.Level",
    "Rate",
    "Depth",
    "LoFilter",
    "HiFilter",
};

static const char* const kDigitalChorusSymbols[kParamCount] = {
    "elevel",
    "rate",
    "depth",
    "lofilter",
    "hifilter",
};

static const float kDigitalChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kDigitalChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDigitalChorusDef[kParamCount] = { 0.42f, 0.12f, 0.62f, 0.24f, 0.25f };

#endif // DIGITAL_CHORUS_PARAMS_H
