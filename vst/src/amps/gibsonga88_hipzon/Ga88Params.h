#ifndef GA88_PARAMS_H
#define GA88_PARAMS_H

// "Hipzon GA-88" — Gibson GA-88S (stereo) front panel, as the game models it:
//   Volume : channel volume — drives the 4x 6BQ5 (EL84) push-pull power (~30 W
//            stereo; modeled mono). Big clean PA-style headroom.
//   Bass   : low end of the passive tone stack.
//   Treble : high end of the passive tone stack.
//   (Real chassis is a stereo PA amp with an OFF/STANDBY/STEREO/MONO function
//    switch; RS exposes only Volume/Bass/Treble.)
enum Ga88ParamId {
    kVolume = 0, kBass, kTreble,
    kParamCount
};

static const char* const kGa88Names[kParamCount]   = { "Volume", "Bass", "Treble" };
static const char* const kGa88Symbols[kParamCount] = { "volume", "bass", "treble" };
static const float kGa88Min[kParamCount] = { 0,0,0 };
static const float kGa88Max[kParamCount] = { 1,1,1 };
static const float kGa88Def[kParamCount] = { 0.60f, 0.50f, 0.50f };

#endif // GA88_PARAMS_H
