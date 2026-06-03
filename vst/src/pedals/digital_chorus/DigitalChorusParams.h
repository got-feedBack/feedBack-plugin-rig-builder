#ifndef DIGITAL_CHORUS_PARAMS_H
#define DIGITAL_CHORUS_PARAMS_H

enum DigitalChorusParamId
{
    kRate = 0,
    kDepth,
    kLoFilter,
    kHiFilter,
    kMix,
    kParamCount
};

static const char* const kDigitalChorusNames[kParamCount] = {
    "Rate",
    "Depth",
    "LoFilter",
    "HiFilter",
    "Mix",
};

static const char* const kDigitalChorusSymbols[kParamCount] = {
    "rate",
    "depth",
    "lofilter",
    "hifilter",
    "mix",
};

static const float kDigitalChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kDigitalChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDigitalChorusDef[kParamCount] = { 0.10f, 0.65f, 0.22f, 0.22f, 0.35f };

#endif // DIGITAL_CHORUS_PARAMS_H
