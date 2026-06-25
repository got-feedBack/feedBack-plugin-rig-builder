#ifndef SEND_IN_THE_CLONES_PARAMS_H
#define SEND_IN_THE_CLONES_PARAMS_H

enum SendInTheClonesParamId
{
    kRate = 0,
    kDepth,
    kChVib,
    kFlange,
    kParamCount
};

static const char* const kSitcNames[kParamCount] = {
    "Rate",
    "Depth",
    "ChVib",
    "Flange",
};

static const char* const kSitcSymbols[kParamCount] = {
    "rate",
    "depth",
    "chvib",
    "flange",
};

static const float kSitcMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kSitcMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kSitcDef[kParamCount] = { 0.26f, 0.48f, 0.38f, 0.0f };

#endif // SEND_IN_THE_CLONES_PARAMS_H
