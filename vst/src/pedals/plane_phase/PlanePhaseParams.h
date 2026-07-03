#ifndef PLANE_PHASE_PARAMS_H
#define PLANE_PHASE_PARAMS_H

enum PlanePhaseParamId
{
    kRate = 0,
    kDepth,
    kMix,
    kParamCount
};

static const char* const kPlanePhaseNames[kParamCount] = {
    "Rate",
    "Depth",
    "Mix",
};

static const char* const kPlanePhaseSymbols[kParamCount] = {
    "rate",
    "depth",
    "mix",
};

static const float kPlanePhaseMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kPlanePhaseMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kPlanePhaseDef[kParamCount] = { 0.34f, 0.46f, 0.22f };

#endif // PLANE_PHASE_PARAMS_H
