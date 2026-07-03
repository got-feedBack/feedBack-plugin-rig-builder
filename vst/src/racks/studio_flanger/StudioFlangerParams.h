#ifndef STUDIO_FLANGER_PARAMS_H
#define STUDIO_FLANGER_PARAMS_H

// the game "Studio Flanger" rack — a stereo flanger. Knobs:
//   Rate  = LFO speed (RS stores Hz, ~0.3 .. 0.6)
//   Depth = sweep depth
//   Regen = feedback / regeneration (the metallic jet)
//   Tone  = low-pass on the wet/feedback (dark .. bright)
//   Mix   = wet/dry blend
enum StudioFlangerParamId { kRate = 0, kDepth, kRegen, kTone, kMix, kParamCount };

static const char* const kStudioFlangerNames[kParamCount]   = { "Rate", "Depth", "Regen", "Tone", "Mix" };
static const char* const kStudioFlangerSymbols[kParamCount] = { "rate", "depth", "regen", "tone", "mix" };

static const float kStudioFlangerMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioFlangerMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioFlangerDef[kParamCount] = { 0.08f, 0.60f, 0.50f, 0.70f, 0.40f };

#endif // STUDIO_FLANGER_PARAMS_H
