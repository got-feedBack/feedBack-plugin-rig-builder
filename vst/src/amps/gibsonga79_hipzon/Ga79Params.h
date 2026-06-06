#ifndef GA79_PARAMS_H
#define GA79_PARAMS_H

// "Hipzon GA-79 RVT" — Gibson GA-79 RVT Multi Stereo front panel, as modeled:
//   Volume : channel volume — drives the 4x 6BQ5 (EL84) push-pull (stereo amp,
//            modeled mono; ~2x 17 W).
//   Bass / Treble : the passive tone stack.
//   Reverb : the valve spring reverb (the 7199 reverb section) blend.
//   Speed / Depth : the on-board tremolo (oscillator rate + intensity).
//   (Real chassis is stereo with Channel 1/2, Frequency/Depth, Reverb, a
//    Stereo/Mono switch; RS exposes only Volume(Volume 1)/Bass/Treble.)
enum Ga79ParamId {
    kVolume = 0, kBass, kTreble, kReverb, kSpeed, kDepth,
    kParamCount
};

static const char* const kGa79Names[kParamCount]   = { "Volume", "Bass", "Treble", "Reverb", "Speed", "Depth" };
static const char* const kGa79Symbols[kParamCount] = { "volume", "bass", "treble", "reverb", "speed", "depth" };
static const float kGa79Min[kParamCount] = { 0,0,0,0,0,0 };
static const float kGa79Max[kParamCount] = { 1,1,1,1,1,1 };
// Volume 0.6; tone flat; Reverb 0.25; Speed 0.4; Depth 0 (tremolo off by default).
static const float kGa79Def[kParamCount] = { 0.60f, 0.50f, 0.50f, 0.25f, 0.40f, 0.00f };

#endif // GA79_PARAMS_H
