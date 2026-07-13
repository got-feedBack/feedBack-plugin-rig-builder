#ifndef AUTO_FILTER_PARAMS_H
#define AUTO_FILTER_PARAMS_H

// Mu-Tron III panel (Gain/Peak/Mode/Range/Direction) + Attack/Release added:
// the real pedal has FIXED opto times (NSL-32 ~55/90 ms) and no such pots, but
// the game's Auto Tone gear DOES author Attack/Release per tone — dropping them
// (the old mapping) made slow-filter tones play wrong. The pots SCALE the opto
// times around the authentic values (0.5 = stock Mu-Tron).
enum AutoFilterParamId
{
    kGain = 0,
    kPeak,
    kMode,
    kRange,
    kDirection,
    kAttack,
    kRelease,
    kParamCount
};

static const char* const kAutoFilterNames[kParamCount] = {
    "Gain",
    "Peak",
    "Mode",
    "Range",
    "Direction",
    "Attack",
    "Release",
};

static const char* const kAutoFilterSymbols[kParamCount] = {
    "gain",
    "peak",
    "mode",
    "range",
    "direction",
    "attack",
    "release",
};

static const float kAutoFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAutoFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAutoFilterDef[kParamCount] = { 0.46f, 0.56f, 0.50f, 1.0f, 1.0f, 0.5f, 0.5f };

#endif // AUTO_FILTER_PARAMS_H
