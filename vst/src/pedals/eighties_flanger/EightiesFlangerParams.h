#ifndef EIGHTIES_FLANGER_PARAMS_H
#define EIGHTIES_FLANGER_PARAMS_H

enum EightiesFlangerParamId
{
    kManual = 0,
    kWidth,
    kSpeed,
    kRegen,
    kParamCount
};

static const char* const kEightiesFlangerNames[kParamCount] = {
    "Manual",
    "Width",
    "Speed",
    "Regen",
};

static const char* const kEightiesFlangerSymbols[kParamCount] = {
    "manual",
    "width",
    "speed",
    "regen",
};

static const float kEightiesFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kEightiesFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kEightiesFlangerDef[kParamCount] = { 0.48f, 0.52f, 0.22f, 0.20f };

#endif // EIGHTIES_FLANGER_PARAMS_H
