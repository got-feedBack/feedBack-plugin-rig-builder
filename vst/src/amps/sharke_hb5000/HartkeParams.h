#ifndef HARTKE_PARAMS_H
#define HARTKE_PARAMS_H

// "Sharke HB5000" — Hartke HA5000 front panel, 1:1:
//   Inputs : Passive / Active (the Active jack pads hot active basses harder).
//   Tube   : level of the 12AX7 tube preamp path (warm, soft).
//   Solid State : level of the solid-state preamp path (clean, punchy). The two
//                 blend — the HA5000's signature dual-preamp mix.
//   Compression : built-in compressor amount.
//   10-band graphic EQ : 30/64/125/250/500/1k/2k/3k/5k/8k Hz, +/-12 dB,
//                        engaged by the EQ In switch.
//   Low Pass / High Pass : variable LPF / HPF tone filters.
//   Volume : master output.
enum HartkeParamId {
    kTube = 0, kSolid, kComp, kLowPass, kHighPass, kVolume,    // knobs
    kEq30, kEq64, kEq125, kEq250, kEq500, kEq1k, kEq2k, kEq4k, kEq8k, kEq16k,  // graphic EQ
    kActive, kEqIn,                                            // switches
    kParamCount
};
static const int kFirstEq = kEq30;     // 10 EQ bands are contiguous from here
static const int kNumEq = 10;
static const float kEqFreqs[kNumEq] = { 30.f, 64.f, 125.f, 250.f, 500.f,
                                        1000.f, 2000.f, 3000.f, 5000.f, 8000.f };

static const char* const kHartkeNames[kParamCount] = {
    "Tube", "Solid State", "Compression", "Low Pass", "High Pass", "Volume",
    "30 Hz", "64 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "3 kHz", "5 kHz", "8 kHz",
    "Active", "EQ In"
};
static const char* const kHartkeSymbols[kParamCount] = {
    "tube", "solid", "comp", "lowpass", "highpass", "volume",
    "eq30", "eq64", "eq125", "eq250", "eq500", "eq1k", "eq2k", "eq3k", "eq5k", "eq8k",
    "active", "eqin"
};
static const float kHartkeMin[kParamCount] = { 0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0 };
static const float kHartkeMax[kParamCount] = { 1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1, 1,1 };
// Tube/Solid 0.5; Comp 0 (off); Low Pass / High Pass 0.5 (CENTERED = open/off —
// LP cuts highs turning down, HP cuts lows turning up); Volume 0.7; EQ bands 0.5
// (flat); Active off; EQ In on.
static const float kHartkeDef[kParamCount] = {
    0.50f, 0.50f, 0.00f, 0.50f, 0.50f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 1.00f
};

#endif // HARTKE_PARAMS_H
