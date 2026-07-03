#ifndef MARSHALL_SUPERVIBE_PARAMS_H
#define MARSHALL_SUPERVIBE_PARAMS_H

enum MarshallSupervibeParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kWave,
    kParamCount
};

static const char* const kMarshallSupervibeNames[kParamCount] = {
    "Rate",
    "Depth",
    "Tone",
    "Sweep",
};

static const char* const kMarshallSupervibeSymbols[kParamCount] = {
    "rate",
    "depth",
    "tone",
    "sweep",
};

static const float kMarshallSupervibeMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMarshallSupervibeMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kMarshallSupervibeDef[kParamCount] = { 0.24f, 0.56f, 0.62f, 0.52f };

#endif // MARSHALL_SUPERVIBE_PARAMS_H
