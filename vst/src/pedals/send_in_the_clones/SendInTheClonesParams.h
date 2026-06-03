#ifndef SEND_IN_THE_CLONES_PARAMS_H
#define SEND_IN_THE_CLONES_PARAMS_H

enum SendInTheClonesParamId
{
    kClones = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kSitcNames[kParamCount] = {
    "Clones",
    "Depth",
    "Mix",
};

static const char* const kSitcSymbols[kParamCount] = {
    "clones",
    "depth",
    "mix",
};

static const float kSitcMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kSitcMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kSitcDef[kParamCount] = { 0.18f, 0.35f, 0.33f };

#endif // SEND_IN_THE_CLONES_PARAMS_H
