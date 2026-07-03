#ifndef DIGITAL_VERB_PARAMS_H
#define DIGITAL_VERB_PARAMS_H

enum DigitalVerbParamId
{
    kTime = 0,
    kMix,
    kDepth,
    kTone,
    kParamCount
};

static const char* const kDigitalVerbNames[kParamCount] = {
    "Time",
    "Mix",
    "Depth",
    "Tone",
};

static const char* const kDigitalVerbSymbols[kParamCount] = {
    "time",
    "mix",
    "depth",
    "tone",
};

static const float kDigitalVerbMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kDigitalVerbMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDigitalVerbDef[kParamCount] = { 0.52f, 0.35f, 0.43f, 0.50f };

#endif // DIGITAL_VERB_PARAMS_H
