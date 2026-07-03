#ifndef SILLA_PARAMS_H
#define SILLA_PARAMS_H

// "Silla Boogie 400" — Mesa/Boogie Bass 400+ front panel, 1:1:
//   Volume 1 / Volume 2 : the two input-channel volumes (pull = Bright cap).
//   Middle / Bass / Treble : the passive tone stack (Bass/Treble pull = Shift,
//                            re-tuning their corner frequency).
//   Master : output level into the 12x 6L6 push-pull (~500 W, huge clean tube
//            headroom).
//   6-band Graphic EQ : 40/100/250/625/1560/3900 Hz, +/-12 dB, EQ In/Out.
enum SillaParamId {
    kVol1 = 0, kVol2, kMiddle, kBass, kTreble, kMaster,        // knobs
    kEq40, kEq100, kEq250, kEq625, kEq1560, kEq3900,           // graphic EQ
    kEqIn, kBright1, kBright2, kBassShift, kTrebShift,         // switches
    kParamCount
};
static const int kFirstEq = kEq40;
static const int kNumEq = 6;
static const float kEqFreqs[kNumEq] = { 40.f, 100.f, 250.f, 625.f, 1560.f, 3900.f };

static const char* const kSillaNames[kParamCount] = {
    "Volume 1", "Volume 2", "Middle", "Bass", "Treble", "Master",
    "40 Hz", "100 Hz", "250 Hz", "625 Hz", "1560 Hz", "3900 Hz",
    "EQ In", "Bright 1", "Bright 2", "Bass Shift", "Treble Shift"
};
static const char* const kSillaSymbols[kParamCount] = {
    "vol1", "vol2", "middle", "bass", "treble", "master",
    "eq40", "eq100", "eq250", "eq625", "eq1560", "eq3900",
    "eqin", "bright1", "bright2", "bassshift", "trebshift"
};
static const float kSillaMin[kParamCount] = { 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0 };
static const float kSillaMax[kParamCount] = { 1,1,1,1,1,1, 1,1,1,1,1,1, 1,1,1,1,1 };
// Vol1 0.5; Vol2 0.3; tone stack flat 0.5; Master 0.7; EQ flat 0.5; EQ In on;
// pull switches off.
static const float kSillaDef[kParamCount] = {
    0.50f, 0.30f, 0.50f, 0.50f, 0.50f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    1.00f, 0.00f, 0.00f, 0.00f, 0.00f
};

#endif // SILLA_PARAMS_H
