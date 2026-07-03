#ifndef MOD_DELAY_PARAMS_H
#define MOD_DELAY_PARAMS_H

enum ModDelayParamId
{
    kDelayTime = 0,
    kRegen,
    kDelayLevel,
    kSpeed,
    kWidth,
    kParamCount
};

static const char* const kModDelayNames[kParamCount] = {
    "Delay Time",
    "Regen",
    "Delay Level",
    "Speed",
    "Width",
};

static const char* const kModDelaySymbols[kParamCount] = {
    "delay_time",
    "regen",
    "delay_level",
    "speed",
    "width",
};

static const float kModDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kModDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kModDelayDef[kParamCount] = {
    (360.0f - 20.0f) / (900.0f - 20.0f),
    0.28f,
    0.28f,
    0.4f / 3.5f,
    0.39f,
};

#endif // MOD_DELAY_PARAMS_H
