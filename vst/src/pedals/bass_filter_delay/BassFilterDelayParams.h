#ifndef BASS_FILTER_DELAY_PARAMS_H
#define BASS_FILTER_DELAY_PARAMS_H

// Rocksmith "Bass Filter Delay" -> Boss DM-2 analog (BBD) delay. The DM-2's
// repeats are warm and progressively darker (the BBD + companding + anti-alias
// filtering roll the highs off a little more on every pass). Real DM-2 knobs
// are Repeat Rate / Intensity / Echo; Rocksmith exposes Time / Feedback / Mix
// plus a dedicated Filter that sets how dark the repeats are:
//   Time     = delay time (Repeat Rate)
//   Feedback = number of repeats (Intensity)
//   Mix      = wet/dry blend (Echo level)
//   Filter   = tone of the repeats — low-pass in the feedback loop, so each
//              echo darkens cumulatively (the analog/BBD character)
enum BassFilterDelayParamId { kTime = 0, kFeedback, kMix, kFilter, kParamCount };

static const char* const kBassFilterDelayNames[kParamCount]   = { "Time", "Feedback", "Mix", "Filter" };
static const char* const kBassFilterDelaySymbols[kParamCount] = { "time", "feedback", "mix", "filter" };

static const float kBassFilterDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassFilterDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassFilterDelayDef[kParamCount] = { 0.40f, 0.40f, 0.40f, 0.55f };

#endif // BASS_FILTER_DELAY_PARAMS_H
