#ifndef SWOLE_PARAMS_H
#define SWOLE_PARAMS_H

// Real Lazy Sprocket / SG-1 controls. Rocksmith's Smash/Rate are mapped to
// these in data/rs_knob_to_vst_param.json.
enum SwoleParamId
{
    kSensitivity = 0,
    kAttack,
    kParamCount
};

static const char* const kSwoleNames[kParamCount] = {
    "Sensitivity",
    "Attack",
};

static const char* const kSwoleSymbols[kParamCount] = {
    "sensitivity",
    "attack",
};

static const float kSwoleMin[kParamCount] = { 0.0f, 0.0f };
static const float kSwoleMax[kParamCount] = { 1.0f, 1.0f };
static const float kSwoleDef[kParamCount] = { 0.52f, 0.38f };

#endif // SWOLE_PARAMS_H
