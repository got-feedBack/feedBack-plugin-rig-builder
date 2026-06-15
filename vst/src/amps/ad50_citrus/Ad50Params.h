#ifndef AD50_PARAMS_H
#define AD50_PARAMS_H

/*
 * CITRUS AD50 = Orange AD50 (Custom Shop) — a simple British EL34 tube head that
 * "aims to be like the old OR120 with more gain than the AD30". Parody brand
 * "Citrus" (same lineage as the Citrus OR50 / OR100 / AD200). The in-app face
 * must NEVER read "Orange".
 *
 * Front panel (the famous Orange "pics-only" graphics): a single GAIN control
 * (hotter than the OR-series preamp), a 2-BAND EQ (BASS + TREBLE only, NO middle),
 * a PRESENCE (power-amp NFB), a MASTER, plus a footswitchable SUSTAIN boost (an
 * EQ-bypass gain/sustain boost) and a Class A / AB power switch (50W Class AB ->
 * 30W Class A = earlier breakup, more compression). Thick midrange-forward Orange
 * voice with more gain/saturation than the OR100/OR50.
 *
 * the game (Amp_OrangeAD50): RS Gain -> GAIN, Bass/Treble -> the 2-band EQ,
 * Presence -> Presence. Master + Sustain + Class A pinned to musical defaults via
 * _static; all editable by hand.
 */
enum Ad50ParamId
{
    kGain = 0,      // GAIN — preamp gain (hotter than the OR series)  [RS Gain]
    kBass,          // BASS — low-shelf EQ                             [RS Bass]
    kTreble,        // TREBLE — high-shelf EQ                          [RS Treble]
    kPresence,      // PRESENCE — power-amp NFB high-shelf             [RS Presence]
    kMaster,        // MASTER — master volume (into the power amp)
    kSustain,       // SUSTAIN — footswitchable EQ-bypass gain/sustain boost
    kClassA,        // power: Class AB (0, 50W) / Class A (1, 30W)
    kParamCount
};

static const char* const kAd50Names[kParamCount] = {
    "Gain", "Bass", "Treble", "Presence", "Master", "Sustain", "Class A",
};

static const char* const kAd50Symbols[kParamCount] = {
    "gain", "bass", "treble", "presence", "master", "sustain", "classa",
};

static const float kAd50Min[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kAd50Max[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: a thick Orange crunch — Gain past noon, EQ around noon,
// Master past noon, Sustain off, Class AB (50W).
static const float kAd50Def[kParamCount] = {
    0.55f, 0.50f, 0.55f, 0.50f, 0.60f, 0.0f, 0.0f,
};

#endif // AD50_PARAMS_H
