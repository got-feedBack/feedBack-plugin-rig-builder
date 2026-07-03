#ifndef STEREO_ANALOG_VIBE_PARAMS_H
#define STEREO_ANALOG_VIBE_PARAMS_H
// the game "Stereo Analog Vibrato" rack -> Univibe-style 4-stage phase vibe.
//   Speed = LFO rate   Waveform = LFO shape (sine .. square)   Mix = dry/wet
enum StereoAnalogVibeParamId { kSpeed = 0, kWaveform, kMix, kParamCount };
static const char* const kStereoAnalogVibeNames[kParamCount]   = { "Speed", "Waveform", "Mix" };
static const char* const kStereoAnalogVibeSymbols[kParamCount] = { "speed", "waveform", "mix" };
static const float kStereoAnalogVibeMin[kParamCount] = { 0,0,0 };
static const float kStereoAnalogVibeMax[kParamCount] = { 1,1,1 };
static const float kStereoAnalogVibeDef[kParamCount] = { 0.45f, 0.20f, 0.50f };
#endif
