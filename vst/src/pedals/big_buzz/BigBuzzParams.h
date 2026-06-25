#ifndef BUZZ_TOO_PARAMS_H
#define BUZZ_TOO_PARAMS_H

// Big Buzz = Big Muff V1 (triangle). Real front panel = three 100k pots:
// SUSTAIN, TONE, VOLUME (see pedals/buzz 2.jpg). RS exposes Gain + Tone, mapped
// Gain->Sustain and Tone->Tone in rs_knob_to_vst_param.json; Volume has no RS
// knob so it sits at its musical default (the amp pattern: model the whole
// panel, drive the RS-exposed subset, pin the rest).
enum BigBuzzParamId
{
    kSustain = 0,
    kTone,
    kVolume,
    kParamCount
};

static const char* const kBigBuzzNames[kParamCount] = {
    "Sustain",
    "Tone",
    "Volume",
};

static const char* const kBigBuzzSymbols[kParamCount] = {
    "sustain",
    "tone",
    "volume",
};

static const float kBigBuzzMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kBigBuzzMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kBigBuzzDef[kParamCount] = { 0.64f, 0.46f, 0.62f };

#endif // BUZZ_TOO_PARAMS_H
