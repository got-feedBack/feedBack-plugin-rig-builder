#ifndef STUDIO_PITCH_PARAMS_H
#define STUDIO_PITCH_PARAMS_H
// the game "Studio Pitch" rack -> pitch shifter. Knobs:
//   Pitch = shift amount (0.5 = unison; down an octave .. up an octave)
//   Tone  = low-pass on the shifted voice   Mix = dry/wet   Pan = wet L/R pan
enum StudioPitchParamId { kPitch = 0, kTone, kMix, kPan, kParamCount };
static const char* const kStudioPitchNames[kParamCount]   = { "Pitch", "Tone", "Mix", "Pan" };
static const char* const kStudioPitchSymbols[kParamCount] = { "pitch", "tone", "mix", "pan" };
static const float kStudioPitchMin[kParamCount] = { 0,0,0,0 };
static const float kStudioPitchMax[kParamCount] = { 1,1,1,1 };
static const float kStudioPitchDef[kParamCount] = { 0.50f, 0.60f, 0.40f, 0.50f };
#endif
