#ifndef QTRON_PARAMS_H
#define QTRON_PARAMS_H

// Shared parameter metadata for the plugin + its UI (keeps them in sync).
// Param NAMES match Rocksmith's Auto Tone knobs (FilterType/Res/Sens/Attack/
// Release) so the mapping is 1:1; Mix is an extra dry/wet; Range/Boost exist
// but have no knob (fixed via the preset state). Enum IDs stay internal.
enum QTronParamId { kMode = 0, kAttack, kRelease, kRange, kPeak, kMix, kGain, kBoost, kParamCount };

static const char* const kQTronNames[kParamCount]   = { "FilterType", "Attack", "Release", "Range", "Res", "Mix", "Sens", "Boost" };
static const char* const kQTronSymbols[kParamCount] = { "filtertype", "attack", "release", "range", "res", "mix", "sens", "boost" };
static const float kQTronMin[kParamCount] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
static const float kQTronMax[kParamCount] = { 2.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
// Defaults: FilterType=Band Pass(1), Attack≈3.7ms, Release≈42ms, Range=High,
// Res=0.4, Mix=0.5, Sens=0.8, Boost=0.2
static const float kQTronDef[kParamCount] = { 1.0f, 0.25f, 0.40f, 0.9f, 0.4f, 0.5f, 0.8f, 0.2f };

#endif // QTRON_PARAMS_H
