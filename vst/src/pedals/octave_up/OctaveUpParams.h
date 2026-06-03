#ifndef OCTAVE_UP_PARAMS_H
#define OCTAVE_UP_PARAMS_H

enum OctaveUpParamId
{
    kTone = 0,
    kMix,
    kParamCount
};

static const char* const kOctaveUpNames[kParamCount] = {
    "Tone",
    "Mix",
};

static const char* const kOctaveUpSymbols[kParamCount] = {
    "tone",
    "mix",
};

static const float kOctaveUpMin[kParamCount] = { 0.0f, 0.0f };
static const float kOctaveUpMax[kParamCount] = { 1.0f, 1.0f };
static const float kOctaveUpDef[kParamCount] = { 0.65f, 0.45f };

#endif // OCTAVE_UP_PARAMS_H
