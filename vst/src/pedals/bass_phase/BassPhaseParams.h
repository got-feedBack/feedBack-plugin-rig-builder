#ifndef BASS_PHASE_PARAMS_H
#define BASS_PHASE_PARAMS_H

// the game "Bass Phase" -> a 4-knob analog bass phaser (MXR Phase 90 / 100 +
// Boss PH-3 lineage, adapted for bass). A chain of swept first-order all-pass
// stages whose break frequency is moved by an LFO; summing the phased signal
// with the dry signal creates the moving notches. On bass the wet path is
// gently high-passed so the low fundamentals stay solid instead of washing out.
// the game knobs (from the gear art): Rate / Depth / Mix / Filter.
//   Rate   = LFO speed
//   Depth  = sweep depth (how far the notches travel) + a little regeneration
//   Mix    = dry/wet blend (full = classic 50/50 deep notches)
//   Filter = centre frequency of the sweep (where the notches sit)
enum BassPhaseParamId { kRate = 0, kDepth, kMix, kFilter, kParamCount };

static const char* const kBassPhaseNames[kParamCount]   = { "Rate", "Depth", "Mix", "Filter" };
static const char* const kBassPhaseSymbols[kParamCount] = { "rate", "depth", "mix", "filter" };

static const float kBassPhaseMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassPhaseMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassPhaseDef[kParamCount] = { 0.30f, 0.60f, 0.60f, 0.40f };

#endif // BASS_PHASE_PARAMS_H
