#ifndef BASS_DISTORTION_PARAMS_H
#define BASS_DISTORTION_PARAMS_H

// the game "Bass Distortion" -> Pro Co RAT. The RAT is an LM308 op-amp with
// huge gain into a pair of hard silicon clipping diodes, then its signature
// passive "Filter" low-pass. Real knobs are Distortion / Filter / Volume; RS
// exposes Gain / Tone / Filter, so:
//   Gain   = Distortion (drive into the clipper)
//   Filter = the RAT Filter (post-clip low-pass — dark .. bright)
//   Tone   = pre-clip brightness (the LM308 feedback-cap character: how harsh
//            the grit is going into the diodes) — uses RS's 3rd knob musically
enum BassDistortionParamId { kGain = 0, kTone, kFilter, kParamCount };

static const char* const kBassDistortionNames[kParamCount]   = { "Gain", "Tone", "Filter" };
static const char* const kBassDistortionSymbols[kParamCount] = { "gain", "tone", "filter" };

static const float kBassDistortionMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kBassDistortionMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kBassDistortionDef[kParamCount] = { 0.80f, 0.50f, 0.50f };

#endif // BASS_DISTORTION_PARAMS_H
