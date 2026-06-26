#ifndef DYNAMICS_COMPRESSION_PARAMS_H
#define DYNAMICS_COMPRESSION_PARAMS_H

enum DynamicsCompressionParamId
{
    kOutput = 0,
    kSensitivity,
    kParamCount
};

static const char* const kDynamicsCompressionNames[kParamCount] = {
    "Output",
    "Sensitivity",
};

static const char* const kDynamicsCompressionSymbols[kParamCount] = {
    "output",
    "sensitivity",
};

static const float kDynamicsCompressionMin[kParamCount] = { 0.0f, 0.0f };
static const float kDynamicsCompressionMax[kParamCount] = { 1.0f, 1.0f };
static const float kDynamicsCompressionDef[kParamCount] = { 0.62f, 0.42f };

#endif // DYNAMICS_COMPRESSION_PARAMS_H
