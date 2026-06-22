#ifndef SVT_PARAMS_H
#define SVT_PARAMS_H

// "Sampleg SBT-CL" — Ampeg SVT-CL front panel, 1:1:
//   Inputs : Normal / -15 dB pad (the SVT's second, padded input jack).
//   Gain   : drives the 12AX7 preamp -> the SVT growl when pushed.
//   Ultra Lo : fixed loudness contour (boost deep lows + highs, scoop ~500 Hz).
//   Ultra Hi : presence/treble boost (adds the SVT clank/bite).
//   Bass   : low shelf (~70 Hz).
//   Midrange : peaking cut/boost at the Frequency-selected centre.
//   Frequency: 5-position midrange selector — 220 / 450 / 800 / 1600 / 3000 Hz.
//   Treble : high shelf (~5 kHz).
//   Master : output level.
enum SvtParamId {
    kGain = 0, kBass, kMidrange, kFreq, kTreble, kMaster,   // knobs
    kPad, kUltraLo, kUltraHi,                               // switches
    kParamCount
};

static const char* const kSvtNames[kParamCount] = {
    "Gain", "Bass", "Midrange", "Frequency", "Treble", "Master",
    "-15dB", "Ultra Lo", "Ultra Hi"
};
static const char* const kSvtSymbols[kParamCount] = {
    "gain", "bass", "midrange", "frequency", "treble", "master",
    "pad", "ultralo", "ultrahi"
};
static const float kSvtMin[kParamCount] = { 0,0,0,0,0,0, 0,0,0 };
static const float kSvtMax[kParamCount] = { 1,1,1,1,1,1, 1,1,1 };
// Tone knobs flat at 0.5; Frequency 0.5 -> the centre (800 Hz) detent; Gain
// 0.5; Master 0.7 ~ unity; switches off.
static const float kSvtDef[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.70f,
    1.00f, 0.00f, 0.00f   // -15dB pad ON by default (padded input)
};

// The 5 midrange-selector centre frequencies (Hz), in panel order.
static const float kSvtMidFreqs[5] = { 220.f, 450.f, 800.f, 1600.f, 3000.f };

#endif // SVT_PARAMS_H
