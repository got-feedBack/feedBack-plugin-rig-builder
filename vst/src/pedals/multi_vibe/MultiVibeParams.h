#ifndef MULTI_VIBE_PARAMS_H
#define MULTI_VIBE_PARAMS_H

enum MultiVibeParamId
{
    kSpeed = 0,
    kMix,        // display "Depth" — pitch-modulation excursion (not a dry/wet mix)
    kWaveform,   // display "Rise Time" — unlatch bloom time
    kMode,       // real VB-2 3-way: Unlatch / Bypass / Latch
    kParamCount
};

static const char* const kMultiVibeNames[kParamCount] = {
    "Rate",
    "Depth",
    "Rise Time",
    "Mode",
};

static const char* const kMultiVibeSymbols[kParamCount] = {
    "rate",
    "depth",
    "risetime",
    "mode",
};

static const float kMultiVibeMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMultiVibeMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
// Bottom selector position (0) is LATCH, the normal continuous-vibrato mode.
static const float kMultiVibeDef[kParamCount] = { 0.38f, 0.50f, 0.18f, 0.0f };

#endif // MULTI_VIBE_PARAMS_H
