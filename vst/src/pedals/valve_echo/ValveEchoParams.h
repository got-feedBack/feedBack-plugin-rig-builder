#ifndef VALVE_ECHO_PARAMS_H
#define VALVE_ECHO_PARAMS_H

enum ValveEchoParamId
{
    kDrumSpeed = 0,
    kSwell,
    kEchoVolume,
    kTone,
    kHeads,
    kParamCount
};

static const char* const kValveEchoNames[kParamCount] = {
    "Drum Speed",
    "Swell",
    "Echo Volume",
    "Tone",
    "Heads",
};

static const char* const kValveEchoSymbols[kParamCount] = {
    "drum_speed",
    "swell",
    "echo_volume",
    "tone",
    "heads",
};

static const float kValveEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kValveEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kValveEchoDef[kParamCount] = {
    (450.0f - 70.0f) / (760.0f - 70.0f),
    0.28f,
    0.24f,
    0.55f,
    1.0f,
};

#endif // VALVE_ECHO_PARAMS_H
