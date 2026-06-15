#ifndef OIL_CAN_ECHO_PARAMS_H
#define OIL_CAN_ECHO_PARAMS_H

// the game "Oil Can Echo" -> Tel-Ray / OK Pacemaker oil-can style echo.
// the game exposes only Time, Feedback, and Mix.
enum OilCanEchoParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kOilCanEchoNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kOilCanEchoSymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kOilCanEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kOilCanEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kOilCanEchoDef[kParamCount] = {
    260.0f / 2000.0f,
    0.55f,
    0.34f,
};

#endif // OIL_CAN_ECHO_PARAMS_H
