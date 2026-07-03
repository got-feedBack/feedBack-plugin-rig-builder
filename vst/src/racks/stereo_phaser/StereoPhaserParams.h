#ifndef STEREO_PHASER_PARAMS_H
#define STEREO_PHASER_PARAMS_H
// the game "Stereo Phaser" rack -> Boss RPH-10 Micro Rack inspired phaser.
// The real panel has Manual, Rate, Depth, Mode and Feedback. the game exposes:
//   Rate  = RPH-10 RATE, existing mapping treats this as normalized 0.1..6 Hz
//   Depth = RPH-10 DEPTH plus internal Mode I/II/III crossfade
//   Mix   = wet/dry balance, with internal Feedback fixed to a musical default
enum StereoPhaserParamId { kRate = 0, kDepth, kMix, kParamCount };
static const char* const kStereoPhaserNames[kParamCount]   = { "Rate", "Depth", "Mix" };
static const char* const kStereoPhaserSymbols[kParamCount] = { "rate", "depth", "mix" };
static const float kStereoPhaserMin[kParamCount] = { 0,0,0 };
static const float kStereoPhaserMax[kParamCount] = { 1,1,1 };
static const float kStereoPhaserDef[kParamCount] = { 0.28f, 0.62f, 0.50f };
#endif
