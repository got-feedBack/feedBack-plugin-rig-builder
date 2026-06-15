#ifndef REVERB_PARAMS_H
#define REVERB_PARAMS_H

// Shared params for the three the game reverb racks (Studio Verb / Chamber /
// Plate). They all expose the same four knobs:
//   Time  = decay length
//   Tone  = damping (dark .. bright)
//   Depth = wet tail modulation depth
//   Mix   = wet/dry blend
enum ReverbParamId { kTime = 0, kTone, kDepth, kMix, kParamCount };

static const char* const kReverbNames[kParamCount]   = { "Time", "Tone", "Depth", "Mix" };
static const char* const kReverbSymbols[kParamCount] = { "time", "tone", "depth", "mix" };

static const float kReverbMin[kParamCount] = { 0,0,0,0 };
static const float kReverbMax[kParamCount] = { 1,1,1,1 };
static const float kReverbDef[kParamCount] = { 0.35f, 0.50f, 0.08f, 0.22f };

#endif // REVERB_PARAMS_H
