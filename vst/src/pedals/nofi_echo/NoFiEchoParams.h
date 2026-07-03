#ifndef NOFI_ECHO_PARAMS_H
#define NOFI_ECHO_PARAMS_H

enum NoFiEchoParamId
{
    kDelayTime = 0,
    kRepeat,
    kDelayLevel,
    kRange,
    kMode,
    kParamCount
};

static const char* const kNoFiEchoNames[kParamCount] = {
    "Delay Time",
    "Repeat",
    "Delay Level",
    "Range",
    "Mode",
};

static const char* const kNoFiEchoSymbols[kParamCount] = {
    "delay_time",
    "repeat",
    "delay_level",
    "range",
    "mode",
};

static const float kNoFiEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kNoFiEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kNoFiEchoDef[kParamCount] = {
    (360.0f - 30.0f) / (2600.0f - 30.0f),
    0.24f,
    0.28f,
    0.50f,
    1.0f,
};

#endif // NOFI_ECHO_PARAMS_H
