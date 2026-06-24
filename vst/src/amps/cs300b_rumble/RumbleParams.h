#ifndef RUMBLE_PARAMS_H
#define RUMBLE_PARAMS_H

// "Bender Fumble Bass" — Fender Rumble Bass (1995, drawings 048406/048411) front
// panel, 1:1. A dual-channel all-tube head: two independent preamp channels (A
// vintage / B), each with its own Volume + passive Fender TMB stack (Treble/Bass
// 250k, Middle 25k) and a MID switch (NORM/CUT mid scoop), blended by MIX into the
// shared 12AT7 phase inverter + 6x6550 push-pull power amp.
//   A Volume / A Treble / A Bass / A Middle / A Mid : channel A.
//   B Volume / B Treble / B Bass / B Middle / B Mid : channel B.
//   Mix : blends the two channels (0 = A only, 1 = B only, 0.5 = both).
// See RumbleCore.h for the circuit-real topology + schematic refs.
enum RumbleParamId {
    kAVol = 0, kATreble, kABass, kAMiddle, kAMidCut,   // channel A
    kBVol, kBTreble, kBBass, kBMiddle, kBMidCut,        // channel B
    kMix,                                               // A/B blend
    kParamCount
};

static const char* const kRumbleNames[kParamCount] = {
    "A Volume", "A Treble", "A Bass", "A Middle", "A Mid Cut",
    "B Volume", "B Treble", "B Bass", "B Middle", "B Mid Cut",
    "Mix"
};
static const char* const kRumbleSymbols[kParamCount] = {
    "avol", "atreble", "abass", "amiddle", "amidcut",
    "bvol", "btreble", "bbass", "bmiddle", "bmidcut",
    "mix"
};
static const float kRumbleMin[kParamCount] = { 0,0,0,0,0, 0,0,0,0,0, 0 };
static const float kRumbleMax[kParamCount] = { 1,1,1,1,1, 1,1,1,1,1, 1 };
// Volumes 0.5; EQ flat 0.5; Mid switches off (NORM); Mix 0.5 (both channels).
static const float kRumbleDef[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.50f, 0.00f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.00f,
    0.50f
};

#endif // RUMBLE_PARAMS_H
