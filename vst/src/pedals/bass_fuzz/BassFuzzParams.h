#ifndef BASS_FUZZ_PARAMS_H
#define BASS_FUZZ_PARAMS_H

// Rocksmith "Bass Fuzz" -> EHX Bass Big Muff Pi model. Param NAMES match the
// Rocksmith knobs 1:1: Gain (= Big Muff Sustain, drive into the clipping
// stages), Tone (Big Muff tone stack — LP/HP blend with the mid scoop), and
// Filter (the bass-specific clean low-end blend, so the fuzz keeps its lows).
enum BassFuzzParamId { kGain = 0, kTone, kFilter, kParamCount };

static const char* const kBassFuzzNames[kParamCount]   = { "Gain", "Tone", "Filter" };
static const char* const kBassFuzzSymbols[kParamCount] = { "gain", "tone", "filter" };

static const float kBassFuzzMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kBassFuzzMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
// Defaults ~ a usable Bass Big Muff: lots of sustain, mid tone, some clean low.
static const float kBassFuzzDef[kParamCount] = { 0.80f, 0.55f, 0.45f };

#endif // BASS_FUZZ_PARAMS_H
