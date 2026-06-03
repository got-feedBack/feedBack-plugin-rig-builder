#ifndef BASS_ENBIG_PARAMS_H
#define BASS_ENBIG_PARAMS_H

// Rocksmith "Bass Enbiggenator" -> a fictional Rocksmith pedal (no real-world
// counterpart). Its knobs (Rate / Depth / Mix / Filter) say "LFO-modulated
// filter", so it's modeled as a resonant band-pass that an LFO wobbles up and
// down around a base frequency — a sci-fi "vibe/throb" filter, distinct from
// the envelope-driven Bass Wah.
//   Rate   = LFO speed of the wobble
//   Depth  = how far the filter sweeps (octaves around the base)
//   Mix    = wet/dry blend
//   Filter = base centre frequency the sweep rides on
enum BassEnbigParamId { kRate = 0, kDepth, kMix, kFilter, kParamCount };

static const char* const kBassEnbigNames[kParamCount]   = { "Rate", "Depth", "Mix", "Filter" };
static const char* const kBassEnbigSymbols[kParamCount] = { "rate", "depth", "mix", "filter" };

static const float kBassEnbigMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassEnbigMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassEnbigDef[kParamCount] = { 0.35f, 0.60f, 0.70f, 0.40f };

#endif // BASS_ENBIG_PARAMS_H
