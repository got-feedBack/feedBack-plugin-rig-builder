#ifndef EDEN_PARAMS_H
#define EDEN_PARAMS_H

// "Aiden GT-300" — Eden WT-300 "The Traveler" (Valve-Tech, Twin-Triode tube
// preamp) front panel, 1:1:
//   Gain    : 12AX7 input drive + the Eden opto compressor (Comp/Active).
//   Enhance : Eden contour — scoops the mids, lifts lows + highs.
//   Bass    : low shelf (~50 Hz, +/-15 dB).
//   Semi-Parametric Bass EQ — 3 sweepable bands, each Freq + Level (+/-15 dB):
//     EQ1 30-300 Hz, EQ2 200 Hz-2 kHz, EQ3 1.2-12 kHz.
//   Treble  : high shelf (~5 kHz, +/-15 dB) with EQ-Clip indicator.
//   Master  : output level with the Output-Limit limiter.
enum EdenParamId {
    kGain = 0, kEnhance, kBass,
    kP1Freq, kP1Level, kP2Freq, kP2Level, kP3Freq, kP3Level,
    kTreble, kMaster,
    kParamCount
};

static const char* const kEdenNames[kParamCount] = {
    "Gain", "Enhance", "Bass",
    "EQ1 Freq", "EQ1 Level", "EQ2 Freq", "EQ2 Level", "EQ3 Freq", "EQ3 Level",
    "Treble", "Master"
};
static const char* const kEdenSymbols[kParamCount] = {
    "gain", "enhance", "bass",
    "eq1freq", "eq1level", "eq2freq", "eq2level", "eq3freq", "eq3level",
    "treble", "master"
};
static const float kEdenMin[kParamCount] = { 0,0,0, 0,0,0,0,0,0, 0,0 };
static const float kEdenMax[kParamCount] = { 1,1,1, 1,1,1,1,1,1, 1,1 };
// Gain 0.5; Enhance 0.3 (a touch of the Eden contour); Bass/Treble flat 0.5;
// each EQ band Freq 0.5 (mid sweep) + Level 0.5 (flat); Master 0.7 ~ unity.
static const float kEdenDef[kParamCount] = {
    0.50f, 0.30f, 0.50f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.50f, 0.70f
};

// Semi-parametric band frequency ranges (Hz), from the schematic (VR101/2/3):
//   EQ1 30-300, EQ2 200-2000, EQ3 1200-12000.  fc = lo * (hi/lo)^knob.
static const float kEdenBandLo[3] = {   30.f,  200.f, 1200.f };
static const float kEdenBandHi[3] = {  300.f, 2000.f,12000.f };

#endif // EDEN_PARAMS_H
