#ifndef BASS_PHASE_PARAMS_H
#define BASS_PHASE_PARAMS_H

// Ibanez PH99 style phaser. Real panel controls are Speed, Depth, Feedback,
// and Level; Rocksmith's Rate/Depth/Mix/Filter are translated in
// data/rs_knob_to_vst_param.json.
enum BassPhaseParamId { kSpeed = 0, kDepth, kFeedback, kLevel, kParamCount };

static const char* const kBassPhaseNames[kParamCount]   = { "Speed", "Depth", "Feedback", "Level" };
static const char* const kBassPhaseSymbols[kParamCount] = { "speed", "depth", "feedback", "level" };

static const float kBassPhaseMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassPhaseMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassPhaseDef[kParamCount] = { 0.32f, 0.58f, 0.46f, 0.72f };

#endif // BASS_PHASE_PARAMS_H
