#ifndef SYNTH_FILTER_PARAMS_H
#define SYNTH_FILTER_PARAMS_H
// the game "Synth Filterbank" rack -> Korg MS-20 ESP/KORG35-style filterbank.
//   Sens = ESP threshold/CV depth   Attack/Release = ENV/CV follower times
//   Type = LP / ESP band-pass / HP   Mix = wet amount and internal peak
enum SynthFilterParamId { kSens = 0, kAttack, kRelease, kFilterType, kMix, kParamCount };
static const char* const kSynthFilterNames[kParamCount]   = { "Sens", "Attack", "Release", "Type", "Mix" };
static const char* const kSynthFilterSymbols[kParamCount] = { "sens", "attack", "release", "type", "mix" };
static const float kSynthFilterMin[kParamCount] = { 0,0,0,0,0 };
static const float kSynthFilterMax[kParamCount] = { 1,1,1,1,1 };
static const float kSynthFilterDef[kParamCount] = { 0.62f, 0.12f, 0.35f, 0.28f, 0.58f };
#endif
