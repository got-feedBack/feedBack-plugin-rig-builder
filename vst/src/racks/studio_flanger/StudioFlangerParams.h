#ifndef STUDIO_FLANGER_PARAMS_H
#define STUDIO_FLANGER_PARAMS_H

// the game "Studio Flanger" rack -> Boss RBF-10 Micro Rack style flanger.
// The real RBF-10 panel has Manual, Rate, Depth, Feedback Level and Balance.
// We preserve the five RS automation slots:
//   Rate  = RBF-10 RATE, existing mapping treats this as 0.1..6 Hz normalized
//   Depth = RBF-10 DEPTH sweep depth
//   Regen = RBF-10 F.BACK LEVEL amount (center-click hardware, unipolar here)
//   Tone  = internal Manual/filter trim because RS has no Manual control
//   Mix   = RBF-10 BALANCE direct/effect wet balance
enum StudioFlangerParamId { kRate = 0, kDepth, kRegen, kTone, kMix, kParamCount };

static const char* const kStudioFlangerNames[kParamCount]   = { "Rate", "Depth", "Regen", "Tone", "Mix" };
static const char* const kStudioFlangerSymbols[kParamCount] = { "rate", "depth", "regen", "tone", "mix" };

static const float kStudioFlangerMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioFlangerMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioFlangerDef[kParamCount] = { 0.08f, 0.58f, 0.45f, 0.56f, 0.42f };

#endif // STUDIO_FLANGER_PARAMS_H
