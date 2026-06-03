#ifndef STEREO_TUBE_TREM_PARAMS_H
#define STEREO_TUBE_TREM_PARAMS_H
// Rocksmith "Stereo Tube Trem" rack -> tube tremolo (amplitude modulation).
//   Speed = LFO rate   Mix = depth/intensity   Waveform = LFO shape (sine..square)
enum StereoTubeTremParamId { kSpeed = 0, kMix, kWaveform, kParamCount };
static const char* const kStereoTubeTremNames[kParamCount]   = { "Speed", "Mix", "Waveform" };
static const char* const kStereoTubeTremSymbols[kParamCount] = { "speed", "mix", "waveform" };
static const float kStereoTubeTremMin[kParamCount] = { 0,0,0 };
static const float kStereoTubeTremMax[kParamCount] = { 1,1,1 };
static const float kStereoTubeTremDef[kParamCount] = { 0.45f, 0.50f, 0.20f };
#endif
