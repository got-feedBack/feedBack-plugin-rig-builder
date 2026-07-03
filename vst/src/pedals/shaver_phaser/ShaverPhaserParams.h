#ifndef SHAVER_PHASER_PARAMS_H
#define SHAVER_PHASER_PARAMS_H

enum ShaverPhaserParamId
{
    kRate = 0,
    kDepth,
    kResonance,
    kParamCount
};

static const char* const kShaverPhaserNames[kParamCount] = {
    "Rate",
    "Depth",
    "Resonance",
};

static const char* const kShaverPhaserSymbols[kParamCount] = {
    "rate",
    "depth",
    "resonance",
};

static const float kShaverPhaserMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kShaverPhaserMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kShaverPhaserDef[kParamCount] = { 0.30f, 0.42f, 0.38f };

#endif // SHAVER_PHASER_PARAMS_H
