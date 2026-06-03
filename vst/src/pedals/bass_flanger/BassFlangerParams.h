#ifndef BASS_FLANGER_PARAMS_H
#define BASS_FLANGER_PARAMS_H

// Rocksmith "Bass Flanger" -> Boss BF-2/BF-3 flanger, adapted for bass. A
// flanger is a SHORT modulated delay with FEEDBACK (the resonant "jet"). The
// bass adaptation keeps the low end out of the swept comb + feedback so the
// fundamental stays solid (only the highs flange) — and the delay range is
// tuned for bass, not the BF-2's guitar-tuned BBD clock/component values.
// RS knobs 1:1: Rate, Depth, Filter (= Resonance/feedback), Mix.
enum BassFlangerParamId { kRate = 0, kDepth, kFilter, kMix, kParamCount };

static const char* const kBassFlangerNames[kParamCount]   = { "Rate", "Depth", "Filter", "Mix" };
static const char* const kBassFlangerSymbols[kParamCount] = { "rate", "depth", "filter", "mix" };

static const float kBassFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassFlangerDef[kParamCount] = { 0.25f, 0.50f, 0.50f, 0.60f };

#endif // BASS_FLANGER_PARAMS_H
