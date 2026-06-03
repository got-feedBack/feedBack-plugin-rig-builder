#ifndef AMP_TREM_PARAMS_H
#define AMP_TREM_PARAMS_H

enum AmpTremParamId
{
    kSpeed = 0,
    kDepth,
    kParamCount
};

static const char* const kAmpTremNames[kParamCount] = {
    "Speed",
    "Depth",
};

static const char* const kAmpTremSymbols[kParamCount] = {
    "speed",
    "depth",
};

static const float kAmpTremMin[kParamCount] = { 0.0f, 0.0f };
static const float kAmpTremMax[kParamCount] = { 1.0f, 1.0f };
static const float kAmpTremDef[kParamCount] = { 0.62f, 0.42f };

#endif // AMP_TREM_PARAMS_H
