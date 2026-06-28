#pragma once
#include <cmath>
// Studio Graphic EQ → API 550L (4-band discrete "graphic" EQ). STEPPED like the
// hardware: each band's Frequency snaps to the 7 real API points and Boost/Cut to
// the API detents (0, ±2, ±4, ±6, ±9, ±12 dB). The LF and HF bands switch
// PEAK/SHELF. PROPORTIONAL Q (narrows as gain rises — the API signature). The
// discrete 2510 op-amps + output transformer give it COLOUR (added in the DSP —
// the 550 is NOT transparent like the GML 8200).
//
// The 4 API bands reuse the RS knob names (Bass/LoMid/HiMid/Treble → LF/LMF/HMF/
// HF) so the game drives them; the game's 5th (Mid) band is unused (the 550L is
// 4-band). Freq ranges MUST match the apply_vst_state 'studiographiceq' block.
enum { gBass, gBassFreq, gBassShelf,
       gLoMid, gLoMidFreq,
       gHiMid, gHiMidFreq,
       gTreble, gTrebleFreq, gTrebleShelf, gNumParams };

static const char* const kSgNames[gNumParams] = {
    "Bass", "BassFreq", "BassShelf",
    "LoMid", "LoMidFreq",
    "HiMid", "HiMidFreq",
    "Treble", "TrebleFreq", "TrebleShelf"
};

// Defaults: 0 dB, mid freq point; LF/HF default to SHELF on (param 1.0).
static const float kSgDef[gNumParams] = {
    0.5f, 0.5f, 1.0f,   // LF (Bass): 0 dB, shelf ON
    0.5f, 0.5f,         // LMF (LoMid)
    0.5f, 0.5f,         // HMF (HiMid)
    0.5f, 0.5f, 1.0f    // HF (Treble): 0 dB, shelf ON
};

// API 550L stepped Boost/Cut detents (dB).
static const float kSgGain[11] = { -12,-9,-6,-4,-2,0,2,4,6,9,12 };
// 7 stepped frequency points per band (the real 550L dial markings).
static const float kSgLF[7]  = {   30,   40,   50,  100,  200,  300,   400 };
static const float kSgLMF[7] = {   75,  150,  180,  240,  500,  700,  1000 };
static const float kSgHMF[7] = {  800, 1500, 3000, 5000, 8000,10000, 12500 };
static const float kSgHF[7]  = { 2500, 5000, 7000,10000,12500,15000, 20000 };

static inline float sgSnap(float v, const float* a, int n) {
    int i = (int)(v * (n - 1) + 0.5f); if (i < 0) i = 0; if (i >= n) i = n - 1; return a[i];
}
static inline float sgGainDb(float v) { return sgSnap(v, kSgGain, 11); }   // detented, ±12 dB
static inline float sgFLF(float v)    { return sgSnap(v, kSgLF,  7); }
static inline float sgFLMF(float v)   { return sgSnap(v, kSgLMF, 7); }
static inline float sgFHMF(float v)   { return sgSnap(v, kSgHMF, 7); }
static inline float sgFHF(float v)    { return sgSnap(v, kSgHF,  7); }
// API proportional Q: wide at low gain, narrowing toward ~2.5 at ±12 dB.
static inline float sgPropQ(float db) { return 0.70f + (fabsf(db) / 12.0f) * 1.80f; }   // 0.7 .. 2.5
static inline bool  sgIsShelf(float v) { return v >= 0.5f; }

#define SG_PLUGIN_LABEL "Studio Graphic EQ"
