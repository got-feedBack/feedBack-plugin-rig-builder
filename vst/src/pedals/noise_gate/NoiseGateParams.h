#ifndef NOISE_GATE_PARAMS_H
#define NOISE_GATE_PARAMS_H

enum NoiseGateParamId
{
    kThresh = 0,
    kRate,
    kParamCount
};

static const char* const kNoiseGateNames[kParamCount] = {
    "Thresh",
    "Rate",
};

static const char* const kNoiseGateSymbols[kParamCount] = {
    "thresh",
    "rate",
};

static const float kNoiseGateMin[kParamCount] = { 0.0f, 0.0f };
static const float kNoiseGateMax[kParamCount] = { 1.0f, 1.0f };
static const float kNoiseGateDef[kParamCount] = { 0.72f, 0.65f };

#endif // NOISE_GATE_PARAMS_H
