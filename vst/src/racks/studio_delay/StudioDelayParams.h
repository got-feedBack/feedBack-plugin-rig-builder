#ifndef STUDIO_DELAY_PARAMS_H
#define STUDIO_DELAY_PARAMS_H

// Rocksmith "Studio Delay" rack — a stereo delay with independent left/right
// times. Knobs:
//   TimeL    = left delay time  (RS stores ms, ~40 .. 360)
//   TimeR    = right delay time (RS stores ms)
//   Feedback = repeats
//   Filter   = low-pass on the repeats (dark .. bright)
//   Mix      = wet/dry blend
enum StudioDelayParamId { kTimeL = 0, kTimeR, kFeedback, kFilter, kMix, kParamCount };

static const char* const kStudioDelayNames[kParamCount]   = { "Time L", "Time R", "Feedback", "Filter", "Mix" };
static const char* const kStudioDelaySymbols[kParamCount] = { "timel", "timer", "feedback", "filter", "mix" };

static const float kStudioDelayMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioDelayMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioDelayDef[kParamCount] = { 0.34f, 0.34f, 0.30f, 0.55f, 0.30f };

#endif // STUDIO_DELAY_PARAMS_H
