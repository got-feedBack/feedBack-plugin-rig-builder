#ifndef TRACER_PARAMS_H
#define TRACER_PARAMS_H

// "Tracer V8" — Trace Elliot V-Type V8 (400 W all-valve bass head) front panel:
//   Inputs : Passive / Active.
//   PREAMP : Gain I (+ Bright pull), Gain II (+ Pull = extra gain), Level.
//   TONE   : Bass (+ Deep pull), Middle (+ Shift pull = mid freq up), Treble.
//   COMP   : On/Off + Level (the built-in compressor).
//   Master : output into the 8x KT88 push-pull (~400 W, huge clean headroom).
enum TracerParamId {
    kGain1 = 0, kGain2, kLevel, kBass, kMiddle, kTreble, kComp, kMaster,  // knobs
    kActive, kBright, kGain2Pull, kDeep, kMidShift, kCompOn,              // switches
    kParamCount
};

static const char* const kTracerNames[kParamCount] = {
    "Gain I", "Gain II", "Level", "Bass", "Middle", "Treble", "Comp Level", "Master",
    "Active", "Bright", "Gain II Pull", "Deep", "Mid Shift", "Compressor"
};
static const char* const kTracerSymbols[kParamCount] = {
    "gain1", "gain2", "level", "bass", "middle", "treble", "comp", "master",
    "active", "bright", "gain2pull", "deep", "midshift", "compon"
};
static const float kTracerMin[kParamCount] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0 };
static const float kTracerMax[kParamCount] = { 1,1,1,1,1,1,1,1, 1,1,1,1,1,1 };
// Gain I 0.5; Gain II 0.4; Level 0.6; tone flat 0.5; Comp 0.4; Master 0.7;
// all pulls/switches off.
static const float kTracerDef[kParamCount] = {
    0.50f, 0.40f, 0.60f, 0.50f, 0.50f, 0.50f, 0.40f, 0.55f,
    0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f
};

#endif // TRACER_PARAMS_H
