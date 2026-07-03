#ifndef BASS_OVERDRIVE_PARAMS_H
#define BASS_OVERDRIVE_PARAMS_H

// Darkglass Microtubes B3K controls. Rocksmith presets still map into these
// names from data/rs_knob_to_vst_param.json.
enum BassOverdriveParamId { kBlend = 0, kDrive, kLevel, kAttack, kGrunt, kParamCount };

static const char* const kBassOverdriveNames[kParamCount]   = { "Blend", "Drive", "Level", "Attack", "Grunt" };
static const char* const kBassOverdriveSymbols[kParamCount] = { "blend", "drive", "level", "attack", "grunt" };

static const float kBassOverdriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassOverdriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassOverdriveDef[kParamCount] = { 0.58f, 0.68f, 0.62f, 0.5f, 0.5f };

#endif // BASS_OVERDRIVE_PARAMS_H
