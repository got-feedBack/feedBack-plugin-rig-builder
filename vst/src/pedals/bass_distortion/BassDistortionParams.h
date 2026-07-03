#ifndef BASS_DISTORTION_PARAMS_H
#define BASS_DISTORTION_PARAMS_H

// Pro Co RAT controls. Rocksmith presets still map into these names from
// data/rs_knob_to_vst_param.json.
enum BassDistortionParamId { kDistortion = 0, kFilter, kVolume, kParamCount };

static const char* const kBassDistortionNames[kParamCount]   = { "Distortion", "Filter", "Volume" };
static const char* const kBassDistortionSymbols[kParamCount] = { "distortion", "filter", "volume" };

static const float kBassDistortionMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kBassDistortionMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kBassDistortionDef[kParamCount] = { 0.78f, 0.45f, 0.62f };

#endif // BASS_DISTORTION_PARAMS_H
