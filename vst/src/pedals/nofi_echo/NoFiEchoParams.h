#ifndef NOFI_ECHO_PARAMS_H
#define NOFI_ECHO_PARAMS_H

enum NoFiEchoParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kNoFiEchoNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kNoFiEchoSymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kNoFiEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kNoFiEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kNoFiEchoDef[kParamCount] = {
    360.0f / 2000.0f,
    0.24f,
    0.28f,
};

#endif // NOFI_ECHO_PARAMS_H
