#ifndef DUAL_RECT_PARAMS_H
#define DUAL_RECT_PARAMS_H

enum DualRectParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kPres,
    kParamCount
};

static const char* const kDualRectNames[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Pres",
};

static const char* const kDualRectSymbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "pres",
};

static const float kDualRectMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kDualRectMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kDualRectDef[kParamCount] = { 0.62f, 0.56f, 0.48f, 0.62f, 0.45f };

#endif // DUAL_RECT_PARAMS_H
