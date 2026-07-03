#ifndef STUDIO_WAH_FILTER_PARAMS_H
#define STUDIO_WAH_FILTER_PARAMS_H
// the game "Studio Wah Filter" rack -> auto/envelope wah filter.
//   Sens = envelope sensitivity   Attack/Release = envelope times
//   Pedal = manual position (when Auto off)   Auto = envelope on/off (switch)
enum StudioWahFilterParamId { kSens = 0, kAttack, kRelease, kPedal, kAuto, kParamCount };
static const char* const kStudioWahFilterNames[kParamCount]   = { "Sens", "Attack", "Release", "Pedal", "Auto" };
static const char* const kStudioWahFilterSymbols[kParamCount] = { "sens", "attack", "release", "pedal", "auto" };
static const float kStudioWahFilterMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioWahFilterMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioWahFilterDef[kParamCount] = { 0.70f, 0.20f, 0.40f, 0.30f, 1.0f };
#endif
