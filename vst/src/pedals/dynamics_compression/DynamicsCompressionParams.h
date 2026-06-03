#ifndef DYNAMICS_COMPRESSION_PARAMS_H
#define DYNAMICS_COMPRESSION_PARAMS_H

enum DynamicsCompressionParamId
{
    kComp = 0,
    kAttack,
    kRelease,
    kParamCount
};

static const char* const kDynamicsCompressionNames[kParamCount] = {
    "Comp",
    "Attack",
    "Release",
};

static const char* const kDynamicsCompressionSymbols[kParamCount] = {
    "comp",
    "attack",
    "release",
};

static const float kDynamicsCompressionMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kDynamicsCompressionMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kDynamicsCompressionDef[kParamCount] = { 0.42f, 0.28f, 0.18f };

#endif // DYNAMICS_COMPRESSION_PARAMS_H
