#ifndef BUZZ_TONE_PARAMS_H
#define BUZZ_TONE_PARAMS_H

// Buzz-Tone = Captain Fuzzle / Maestro FZ-1A (3x 2N1305 germanium, 1.5 V).
// Real panel = two 50kB pots only: ATTACK (Q2 bias/feedback) and VOLUME — see
// pedals/captain fuzzle.gif. There is NO tone control on the real pedal, so RS
// Gain maps to Attack and RS Tone is left unmapped (rs_knob_to_vst_param.json);
// Volume sits at its musical default (no RS pedal-volume knob).
enum BuzzToneParamId
{
    kAttack = 0,
    kVolume,
    kParamCount
};

static const char* const kBuzzToneNames[kParamCount] = {
    "Attack",
    "Volume",
};

static const char* const kBuzzToneSymbols[kParamCount] = {
    "attack",
    "volume",
};

static const float kBuzzToneMin[kParamCount] = { 0.0f, 0.0f };
static const float kBuzzToneMax[kParamCount] = { 1.0f, 1.0f };
static const float kBuzzToneDef[kParamCount] = { 0.78f, 0.60f };

#endif // BUZZ_TONE_PARAMS_H
