#ifndef DUSTUP_PARAMS_H
#define DUSTUP_PARAMS_H

// "Dustup CDN" — Ashdown ABM EVO (Bass Magnifier) front panel, 1:1:
//   Input  : input gain (+ VU). Passive/Active + Push Flat/Shape switches.
//   Bass / Middle / Treble : the main tone knobs.
//   Graphic EQ : 6 bands 100/180/340/1.3k/3.6k/5k Hz (EQ In/Out).
//   Valve Drive : ECC83 clean->grind blend (the Ashdown tube warmth/drive).
//   Sub Harmonics : sub-octave low weight (on/off + level).
//   Comp   : the built-in compressor (on/off + amount).
//   Output : master level into the SS power amp.
enum DustupParamId {
    kInput = 0, kBass, kMiddle, kTreble, kValve, kSub, kComp, kOutput,   // knobs
    kEq100, kEq180, kEq340, kEq1k3, kEq3k6, kEq5k,                       // graphic EQ
    kActive, kShape, kEqIn, kSubOn, kCompOn,                            // switches
    kParamCount
};
static const int kFirstEq = kEq100;
static const int kNumEq = 6;
static const float kEqFreqs[kNumEq] = { 100.f, 180.f, 340.f, 1300.f, 3600.f, 5000.f };

static const char* const kDustupNames[kParamCount] = {
    "Input", "Bass", "Middle", "Treble", "Valve Drive", "Sub Harmonics", "Comp", "Output",
    "100 Hz", "180 Hz", "340 Hz", "1.3 kHz", "3.6 kHz", "5 kHz",
    "Active", "Shape", "EQ In", "Sub", "Compressor"
};
static const char* const kDustupSymbols[kParamCount] = {
    "input", "bass", "middle", "treble", "valve", "sub", "comp", "output",
    "eq100", "eq180", "eq340", "eq1k3", "eq3k6", "eq5k",
    "active", "shape", "eqin", "subon", "compon"
};
static const float kDustupMin[kParamCount] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0 };
static const float kDustupMax[kParamCount] = { 1,1,1,1,1,1,1,1, 1,1,1,1,1,1, 1,1,1,1,1 };
// Input 0.5; tone flat 0.5; Valve 0.2; Sub 0.4; Comp 0.4; Output 0.7; EQ flat
// 0.5; Active/Shape off; EQ In on; Sub/Comp off.
static const float kDustupDef[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.50f, 0.20f, 0.40f, 0.40f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 0.00f, 1.00f, 0.00f, 0.00f
};

#endif // DUSTUP_PARAMS_H
