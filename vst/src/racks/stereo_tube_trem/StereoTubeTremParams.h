#ifndef STEREO_TUBE_TREM_PARAMS_H
#define STEREO_TUBE_TREM_PARAMS_H
// the game "Stereo Tube Trem" rack -> Peavey Valverb-style tube/opto tremolo.
//   Speed    = trem oscillator rate
//   Mix      = opto depth/intensity
//   Waveform = oscillator clipping/shape amount (the Valverb has no exact knob)
enum StereoTubeTremParamId { kSpeed = 0, kMix, kWaveform, kParamCount };
static const char* const kStereoTubeTremNames[kParamCount]   = { "Speed", "Mix", "Waveform" };
static const char* const kStereoTubeTremSymbols[kParamCount] = { "speed", "mix", "waveform" };
static const float kStereoTubeTremMin[kParamCount] = { 0,0,0 };
static const float kStereoTubeTremMax[kParamCount] = { 1,1,1 };
static const float kStereoTubeTremDef[kParamCount] = { 0.36f, 0.48f, 0.24f };
#endif
