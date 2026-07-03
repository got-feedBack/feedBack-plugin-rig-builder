#ifndef VALVE_ECHO_PARAMS_H
#define VALVE_ECHO_PARAMS_H

enum ValveEchoParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kValveEchoNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kValveEchoSymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kValveEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kValveEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kValveEchoDef[kParamCount] = {
    450.0f / 2000.0f,
    0.28f,
    0.24f,
};

#endif // VALVE_ECHO_PARAMS_H
