#ifndef EDEN_PARAMS_H
#define EDEN_PARAMS_H

// "Aiden GT-880" — Eden WT-880 "World Tour" (Valve-Tech Twin-Triode hybrid bass
// head) front panel, 1:1. Same Eden preamp as the WT-300/550 plus the WT-880's
// bi-amp section:
//   Gain    : 12AX7 input drive + Eden opto compressor.
//   Enhance : Eden mid-scoop / low+high lift contour.
//   Bass    : low shelf (~50 Hz, +/-15 dB).
//   Semi-Parametric Bass EQ — Low/Mid/High, each Freq + Level (+/-15 dB):
//     30-300 / 200-2k / 1.2-12k Hz.
//   Treble  : high shelf (~5 kHz, +/-15 dB).
//   Master  : output level.
//   X-Over Freq : bi-amp crossover frequency (100 Hz .. 5 kHz).
//   Balance : bi-amp low/high power balance (tilts lows vs highs at the X-over).
//   X-Over  : engage the bi-amp crossover (off = full-range).
enum EdenParamId {
    kGain = 0, kEnhance, kBass,
    kP1Freq, kP1Level, kP2Freq, kP2Level, kP3Freq, kP3Level,
    kTreble, kMaster, kXoverFreq, kBalance,
    kXoverOn,
    kParamCount
};

static const char* const kEdenNames[kParamCount] = {
    "Gain", "Enhance", "Bass",
    "Low Freq", "Low Level", "Mid Freq", "Mid Level", "High Freq", "High Level",
    "Treble", "Master", "X-Over Freq", "Balance",
    "X-Over"
};
static const char* const kEdenSymbols[kParamCount] = {
    "gain", "enhance", "bass",
    "lowfreq", "lowlevel", "midfreq", "midlevel", "highfreq", "highlevel",
    "treble", "master", "xoverfreq", "balance",
    "xoveron"
};
static const float kEdenMin[kParamCount] = { 0,0,0, 0,0,0,0,0,0, 0,0,0,0, 0 };
static const float kEdenMax[kParamCount] = { 1,1,1, 1,1,1,1,1,1, 1,1,1,1, 1 };
// Gain 0.5; Enhance 0.3; Bass/Treble flat 0.5; each band Freq 0.5 + Level 0.5;
// Master 0.7; X-Over Freq 0.5; Balance 0.5 (centre); X-Over off (full-range).
static const float kEdenDef[kParamCount] = {
    0.50f, 0.30f, 0.50f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.50f, 0.70f, 0.50f, 0.50f,
    0.00f
};

// Semi-parametric band frequency ranges (Hz), from the schematic (VR101/2/3):
static const float kEdenBandLo[3] = {   30.f,  200.f, 1200.f };
static const float kEdenBandHi[3] = {  300.f, 2000.f,12000.f };

#endif // EDEN_PARAMS_H
