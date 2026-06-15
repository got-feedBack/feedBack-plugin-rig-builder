#ifndef SYNTH_FILTER_PARAMS_H
#define SYNTH_FILTER_PARAMS_H
// the game "Synth Filterbank" rack -> envelope-following resonant filter.
//   Sens = envelope sensitivity   Attack/Release = envelope times
//   FilterType = LP / BP / HP (0..1 → 3 zones)   Mix = dry/wet
enum SynthFilterParamId { kSens = 0, kAttack, kRelease, kFilterType, kMix, kParamCount };
static const char* const kSynthFilterNames[kParamCount]   = { "Sens", "Attack", "Release", "Type", "Mix" };
static const char* const kSynthFilterSymbols[kParamCount] = { "sens", "attack", "release", "type", "mix" };
static const float kSynthFilterMin[kParamCount] = { 0,0,0,0,0 };
static const float kSynthFilterMax[kParamCount] = { 1,1,1,1,1 };
static const float kSynthFilterDef[kParamCount] = { 0.70f, 0.15f, 0.40f, 0.0f, 0.60f };
#endif
