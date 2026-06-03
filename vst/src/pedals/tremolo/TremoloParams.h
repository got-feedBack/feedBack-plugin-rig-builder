#ifndef TREMOLO_PARAMS_H
#define TREMOLO_PARAMS_H

enum TremoloParamId
{
    kSpeed = 0,
    kMix,
    kParamCount
};

static const char* const kTremoloNames[kParamCount] = {
    "Speed",
    "Mix",
};

static const char* const kTremoloSymbols[kParamCount] = {
    "speed",
    "mix",
};

static const float kTremoloMin[kParamCount] = { 0.0f, 0.0f };
static const float kTremoloMax[kParamCount] = { 1.0f, 1.0f };
static const float kTremoloDef[kParamCount] = { 0.56f, 0.49f };

#endif // TREMOLO_PARAMS_H
