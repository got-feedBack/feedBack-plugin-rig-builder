#ifndef PLATE_VERB_PARAMS_H
#define PLATE_VERB_PARAMS_H

// the game Pedal_PlateVerb exposes Time, Depth, Mix, and Voice.
enum PlateVerbParamId
{
    kTime = 0,
    kDepth,
    kMix,
    kVoice,
    kParamCount
};

static const char* const kPlateVerbNames[kParamCount] = {
    "Time",
    "Depth",
    "Mix",
    "Voice",
};

static const char* const kPlateVerbSymbols[kParamCount] = {
    "time",
    "depth",
    "mix",
    "voice",
};

static const float kPlateVerbMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kPlateVerbMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kPlateVerbDef[kParamCount] = { 0.52f, 0.55f, 0.30f, 1.0f };

#endif // PLATE_VERB_PARAMS_H
