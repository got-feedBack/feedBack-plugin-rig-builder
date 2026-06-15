#ifndef BASS_CHORUS_PARAMS_H
#define BASS_CHORUS_PARAMS_H

// the game "Bass Chorus" -> Boss CEB-3 Bass Chorus. BBD-style chorus (an LFO
// modulates a short delay, mixed with the dry signal). The CEB-3's bass trick
// is the LOW FILTER: it keeps the low frequencies OUT of the chorus so the
// bass fundamental stays solid/centred (the CE-3 guitar version lacks this).
// We model the chorus behaviour and tune the frequencies for bass — NOT the
// CE-3's guitar-tuned component values.
// RS knobs 1:1: Rate, Depth, LoFilter, Mix.
enum BassChorusParamId { kRate = 0, kDepth, kLoFilter, kMix, kParamCount };

static const char* const kBassChorusNames[kParamCount]   = { "Rate", "Depth", "LoFilter", "Mix" };
static const char* const kBassChorusSymbols[kParamCount] = { "rate", "depth", "lofilter", "mix" };

static const float kBassChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassChorusDef[kParamCount] = { 0.20f, 0.60f, 0.40f, 0.50f };

#endif // BASS_CHORUS_PARAMS_H
