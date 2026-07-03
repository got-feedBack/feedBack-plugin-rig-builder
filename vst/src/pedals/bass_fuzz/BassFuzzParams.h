#ifndef BASS_FUZZ_PARAMS_H
#define BASS_FUZZ_PARAMS_H

// Bass Big Muff Pi controls. Rocksmith presets still map into these names from
// data/rs_knob_to_vst_param.json.
enum BassFuzzParamId { kSustain = 0, kTone, kVolume, kBassDry, kParamCount };

static const char* const kBassFuzzNames[kParamCount]   = { "Sustain", "Tone", "Volume", "Bass/Dry" };
static const char* const kBassFuzzSymbols[kParamCount] = { "sustain", "tone", "volume", "bassdry" };

static const float kBassFuzzMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassFuzzMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassFuzzDef[kParamCount] = { 0.78f, 0.52f, 0.62f, 0.0f };

#endif // BASS_FUZZ_PARAMS_H
