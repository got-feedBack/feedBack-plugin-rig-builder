#ifndef BASS_MULTI_COMP_PARAMS_H
#define BASS_MULTI_COMP_PARAMS_H

enum BassMultiCompParamId
{
    kComp = 0,
    kSens,
    kGain,
    kMode,
    kParamCount
};

static const char* const kBassMultiCompNames[kParamCount] = {
    "Comp",
    "Sens",
    "Gain",
    "Mode",
};

static const char* const kBassMultiCompSymbols[kParamCount] = {
    "comp",
    "sens",
    "gain",
    "mode",
};

static const float kBassMultiCompMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassMultiCompMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassMultiCompDef[kParamCount] = { 0.46f, 0.45f, 0.56f, 0.50f };

#endif // BASS_MULTI_COMP_PARAMS_H
