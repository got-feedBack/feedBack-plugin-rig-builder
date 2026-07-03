#ifndef STUDIO_DELAY_PARAMS_H
#define STUDIO_DELAY_PARAMS_H

// the game "Studio Delay" rack -> dual Boss RDD-10/RDD-20 style digital delay.
// Knobs:
//   TimeL/TimeR = per-channel delay time. RS stores ms as ms/700; DSP clamps
//                 to the RDD service-note range of 0.75..400 ms.
//   Feedback    = feedback level
//   Filter      = RDD Tone control
//   Mix         = mixer delay level / wet-dry blend
enum StudioDelayParamId { kTimeL = 0, kTimeR, kFeedback, kFilter, kMix, kParamCount };

static const char* const kStudioDelayNames[kParamCount]   = { "Time L", "Time R", "Feedback", "Filter", "Mix" };
static const char* const kStudioDelaySymbols[kParamCount] = { "timel", "timer", "feedback", "filter", "mix" };

static const float kStudioDelayMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioDelayMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioDelayDef[kParamCount] = { 0.34f, 0.34f, 0.30f, 0.55f, 0.30f };

#endif // STUDIO_DELAY_PARAMS_H
