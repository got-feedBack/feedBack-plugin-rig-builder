#ifndef STEREO_PHASER_PARAMS_H
#define STEREO_PHASER_PARAMS_H
// the game "Stereo Phaser" rack — a stereo phaser. Knobs: Rate (Hz), Depth, Mix.
enum StereoPhaserParamId { kRate = 0, kDepth, kMix, kParamCount };
static const char* const kStereoPhaserNames[kParamCount]   = { "Rate", "Depth", "Mix" };
static const char* const kStereoPhaserSymbols[kParamCount] = { "rate", "depth", "mix" };
static const float kStereoPhaserMin[kParamCount] = { 0,0,0 };
static const float kStereoPhaserMax[kParamCount] = { 1,1,1 };
static const float kStereoPhaserDef[kParamCount] = { 0.33f, 0.70f, 0.50f };
#endif
