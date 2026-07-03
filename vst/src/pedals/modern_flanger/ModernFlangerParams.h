#ifndef MODERN_FLANGER_PARAMS_H
#define MODERN_FLANGER_PARAMS_H

enum ModernFlangerParamId
{
    kDelayTime = 0,
    kRange,
    kFeedback,
    kDrive,
    kOutput,
    kMix,
    kLfoShape,
    kLfoRate,
    kLfoAmount,
    kParamCount
};

static const char* const kModernFlangerNames[kParamCount] = {
    "Time",
    "Range",
    "Feedback",
    "Drive",
    "Output Level",
    "Mix",
    "LFO Shape",
    "LFO Rate",
    "LFO Amount",
};

static const char* const kModernFlangerSymbols[kParamCount] = {
    "time",
    "range",
    "feedback",
    "drive",
    "output_level",
    "mix",
    "lfo_shape",
    "lfo_rate",
    "lfo_amount",
};

static const float kModernFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kModernFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kModernFlangerDef[kParamCount] = { 0.54f, 0.0f, 0.50f, 0.38f, 0.88f, 0.52f, 0.08f, 0.26f, 0.54f };

#endif // MODERN_FLANGER_PARAMS_H
