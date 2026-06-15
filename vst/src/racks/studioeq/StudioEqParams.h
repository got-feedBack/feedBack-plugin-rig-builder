#pragma once
#include <cmath>
// the game Studio EQ — 4-band parametric (GML-style clean). Param names match
// the RS knobs 1:1. Freq ranges (Hz) and Q range MUST match the apply_vst_state
// ranges for the 'studioeq' stem so RS values reproduce exactly.
enum { kBass, kBassFreq, kLoMid, kLoMidFreq, kLoMidQ, kHiMid, kHiMidFreq, kHiMidQ, kTreble, kTrebleFreq, kNumParams };

static const char* const kSeqNames[kNumParams] = {
    "Bass", "BassFreq", "LoMid", "LoMidFreq", "LoMidQ", "HiMid", "HiMidFreq", "HiMidQ", "Treble", "TrebleFreq"
};

// param 0..1 → value
static inline float seqDb(float v)     { return (v - 0.5f) * 30.0f; }                 // ±15 dB
static inline float seqQ(float v)      { return 0.3f * powf(13.3333f, v); }           // 0.3 .. 4
static inline float seqFBass(float v)  { return 30.0f   * powf(10.0f,    v); }         // 30 .. 300
static inline float seqFLoMid(float v) { return 120.0f  * powf(16.6667f, v); }         // 120 .. 2000
static inline float seqFHiMid(float v) { return 400.0f  * powf(20.0f,    v); }         // 400 .. 8000
static inline float seqFTreble(float v){ return 1500.0f * powf(10.6667f, v); }         // 1500 .. 16000

#define SEQ_PLUGIN_LABEL "Studio EQ"
