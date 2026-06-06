#ifndef GA8_PARAMS_H
#define GA8_PARAMS_H

// "Hipzon GA-8" — Gibson GA-8 Discoverer (GA-8T) front panel:
//   Volume : the Loudness control — drives the 2x 6BM8 (ECL82) push-pull (~10 W).
//   Bass   : low end of the passive tone stack.
//   Treble : high end of the passive tone stack.
//   Speed  : tremolo oscillator rate (the GA-8T "Frequency" control).
//   Depth  : tremolo intensity.
//   (RS exposes Volume(Loudness)/Bass/Treble; Speed/Depth are the amp's tremolo.)
enum Ga8ParamId {
    kVolume = 0, kBass, kTreble, kSpeed, kDepth,
    kParamCount
};

static const char* const kGa8Names[kParamCount]   = { "Volume", "Bass", "Treble", "Speed", "Depth" };
static const char* const kGa8Symbols[kParamCount] = { "volume", "bass", "treble", "speed", "depth" };
static const float kGa8Min[kParamCount] = { 0,0,0,0,0 };
static const float kGa8Max[kParamCount] = { 1,1,1,1,1 };
// Volume 0.6; tone flat; Speed 0.4; Depth 0 (tremolo off by default).
static const float kGa8Def[kParamCount] = { 0.60f, 0.50f, 0.50f, 0.40f, 0.00f };

#endif // GA8_PARAMS_H
