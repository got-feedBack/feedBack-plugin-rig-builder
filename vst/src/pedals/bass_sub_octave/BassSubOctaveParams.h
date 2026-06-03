#ifndef BASS_SUB_OCTAVE_PARAMS_H
#define BASS_SUB_OCTAVE_PARAMS_H

// Rocksmith "Bass Sub Octave" -> Boss OC-2 Octave. The OC-2 is a monophonic
// ANALOG octaver: a comparator squares the input, flip-flops divide that
// square's frequency by two (one octave down), and the divided square is
// amplitude-tracked by the input envelope so the sub follows your dynamics.
// The real pedal has three knobs (OCT2 / Direct Level / OCT1); Rocksmith
// simplifies it to two:
//   Mix  = blend of the generated sub-octave against the dry signal
//   Tone = low-pass on the sub-octave (square → rounder, deeper sub)
enum BassSubOctaveParamId { kMix = 0, kTone, kParamCount };

static const char* const kBassSubOctaveNames[kParamCount]   = { "Mix", "Tone" };
static const char* const kBassSubOctaveSymbols[kParamCount] = { "mix", "tone" };

static const float kBassSubOctaveMin[kParamCount] = { 0.0f, 0.0f };
static const float kBassSubOctaveMax[kParamCount] = { 1.0f, 1.0f };
static const float kBassSubOctaveDef[kParamCount] = { 0.50f, 0.50f };

#endif // BASS_SUB_OCTAVE_PARAMS_H
