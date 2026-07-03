#pragma once
#include <cmath>
// Studio EQ → GML 8200 model. FULL 5-band form factor (the real 8200): Low,
// LoMid, Mid, HiMid, High — each fully parametric with Gain (±15 dB), Freq, and
// Q (0.4..4.0). The Low and High bands shelve at the Q knob's CCW detent (the
// 8200's shelving mode); the three middle bands are always bell.
//
// RS compatibility: the game's Studio EQ sends only Bass/LoMid/HiMid/Treble
// (+ freqs + LoMidQ/HiMidQ). Those param NAMES are kept EXACTLY, so RS
// reproduction is unchanged. The added BassQ/TrebleQ default to the shelf detent
// (Bass/Treble behave as shelves, as before) and the new Mid band defaults flat
// — so an RS-driven tone is identical to the old 4-band EQ, while manual use
// gets the full 8200. Freq/Q ranges for the RS bands MUST match the
// apply_vst_state 'studioeq' ranges.
enum { kBass, kBassFreq, kBassQ,
       kLoMid, kLoMidFreq, kLoMidQ,
       kMid, kMidFreq, kMidQ,
       kHiMid, kHiMidFreq, kHiMidQ,
       kTreble, kTrebleFreq, kTrebleQ, kNumParams };

static const char* const kSeqNames[kNumParams] = {
    "Bass", "BassFreq", "BassQ",
    "LoMid", "LoMidFreq", "LoMidQ",
    "Mid", "MidFreq", "MidQ",
    "HiMid", "HiMidFreq", "HiMidQ",
    "Treble", "TrebleFreq", "TrebleQ"
};

// Defaults: gains 0 dB, freqs mid-range, mid-band Q ~1.26; Low/High Q at the CCW
// shelf detent (0.0) so Bass/Treble are shelves out of the box (matches the
// original 4-band behaviour and the RS-driven sound).
static const float kSeqDef[kNumParams] = {
    0.5f, 0.5f, 0.0f,   // Bass  : 0 dB, mid freq, SHELF
    0.5f, 0.5f, 0.5f,   // LoMid : bell
    0.5f, 0.5f, 0.5f,   // Mid   : flat bell (new band)
    0.5f, 0.5f, 0.5f,   // HiMid : bell
    0.5f, 0.5f, 0.0f    // Treble: 0 dB, mid freq, SHELF
};

// param 0..1 → value
static inline float seqDb(float v)      { return (v - 0.5f) * 30.0f; }            // ±15 dB (8200 spec)
static inline float seqQ(float v)       { return 0.4f * powf(10.0f, v); }         // 0.4 .. 4.0 (8200 spec)
static inline bool  seqIsShelf(float v) { return v <= 0.03f; }                    // Low/High CCW shelf detent
static inline float seqFBass(float v)   { return 30.0f   * powf(10.0f,    v); }   // 30 .. 300   (RS)
static inline float seqFLoMid(float v)  { return 120.0f  * powf(16.6667f, v); }   // 120 .. 2000 (RS)
static inline float seqFMid(float v)    { return 120.0f  * powf(66.6667f, v); }   // 120 .. 8000 (8200 band 3)
static inline float seqFHiMid(float v)  { return 400.0f  * powf(20.0f,    v); }   // 400 .. 8000 (RS)
static inline float seqFTreble(float v) { return 1500.0f * powf(10.6667f, v); }   // 1500 .. 16000 (RS)

#define SEQ_PLUGIN_LABEL "Studio EQ"
