#pragma once
#include <cmath>
// the game Studio Graphic EQ — 5-band sweepable EQ, API-550 style. No Q knob:
// the peaking bands use PROPORTIONAL Q (Q widens at low gain, narrows at high
// gain — the API signature). Bass/Treble are sweepable shelves. Param names
// match RS 1:1. Freq ranges (Hz) MUST match the apply_vst_state 'studiographiceq'
// block. (RS BassFreq/LoMidFreq are Hz; Mid/HiMid/TrebleFreq are kHz → ×1000.)
enum { gBass, gBassFreq, gLoMid, gLoMidFreq, gMid, gMidFreq, gHiMid, gHiMidFreq, gTreble, gTrebleFreq, gNumParams };

static const char* const kSgNames[gNumParams] = {
    "Bass", "BassFreq", "LoMid", "LoMidFreq", "Mid", "MidFreq", "HiMid", "HiMidFreq", "Treble", "TrebleFreq"
};

static inline float sgDb(float v)       { return (v - 0.5f) * 30.0f; }                 // ±15 dB
static inline float sgPropQ(float db)   { return 0.5f + fabsf(db) * 0.1f; }            // API proportional Q: 0.5 .. 2.0
static inline float sgFBass(float v)    { return 40.0f   * powf(10.0f,    v); }         // 40 .. 400
static inline float sgFLoMid(float v)   { return 200.0f  * powf(10.0f,    v); }         // 200 .. 2000
static inline float sgFMid(float v)     { return 300.0f  * powf(10.0f,    v); }         // 300 .. 3000
static inline float sgFHiMid(float v)   { return 800.0f  * powf(10.0f,    v); }         // 800 .. 8000
static inline float sgFTreble(float v)  { return 2000.0f * powf(8.0f,     v); }         // 2000 .. 16000

#define SG_PLUGIN_LABEL "Studio Graphic EQ"
