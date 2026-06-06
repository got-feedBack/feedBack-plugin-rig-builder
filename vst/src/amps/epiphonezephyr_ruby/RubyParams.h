#ifndef RUBY_PARAMS_H
#define RUBY_PARAMS_H

// "Epicall Ruby" — Epiphone (Danelectro-built) Electar Zephyr Amp20, ~1949, as
// Rocksmith models it (3 knobs):
//   Volume : the instrument volume — drives the 2x 6L6G push-pull (~20 W); cranks
//            into warm vintage breakup.
//   Bass   : low end of the passive tone stack.
//   Treble : high end of the passive tone stack.
//   (Real chassis also had a Tone control, Mic input + a tremolo — cosmetic / out
//    of scope; RS exposes only Volume/Bass/Treble.)
enum RubyParamId {
    kVolume = 0, kBass, kTreble,   // knobs
    kParamCount
};

static const char* const kRubyNames[kParamCount] = {
    "Volume", "Bass", "Treble"
};
static const char* const kRubySymbols[kParamCount] = {
    "volume", "bass", "treble"
};
static const float kRubyMin[kParamCount] = { 0,0,0 };
static const float kRubyMax[kParamCount] = { 1,1,1 };
// Volume 0.6; Bass 0.5; Treble 0.5 (flat tone).
static const float kRubyDef[kParamCount] = {
    0.60f, 0.50f, 0.50f
};

#endif // RUBY_PARAMS_H
