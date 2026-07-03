#ifndef TMAX_PARAMS_H
#define TMAX_PARAMS_H

// "Peabey T-Max" — Peavey T-Max "Two Channel Bass System" front panel, 1:1:
//   Input    : Active / Passive (the Active jack pads hot active basses).
//   Tube Pre / Tube Post : drive + level of the 12AX7 tube channel (warm grit).
//   Solid State : drive of the solid-state channel (clean/punchy, with CLIP).
//   Channel Select  : pick Solid State or Tube when not combined.
//   Channel Combine : sum BOTH channels (the T-Max's blended dual-preamp voice).
//   Shelving Low / High : +/-15 dB bass / treble shelves.
//   7-band Graphic EQ : 40/100/250/625/1.6k/4k/10k Hz, +/-15 dB, Graphic In/Out.
//   Balance  : biamp low/high power balance (tilts lows vs highs at the X-over).
//   X-Over   : biamp crossover frequency (100 Hz .. 1 kHz).
//   Master   : output level into the (300 W) solid-state power amp.
enum TMaxParamId {
    kTubePre = 0, kTubePost, kSsPre, kShelfLow, kShelfHigh, kBalance, kXover, kMaster, // knobs
    kEq40, kEq100, kEq250, kEq625, kEq1k6, kEq4k, kEq10k,                              // graphic EQ
    kActive, kChanSel, kChanCombine, kGraphicIn,                                       // switches
    kParamCount
};
static const int kFirstEq = kEq40;     // 7 EQ bands are contiguous from here
static const int kNumEq = 7;
// The 7 graphic-EQ band centres printed on the T-Max panel.
static const float kEqFreqs[kNumEq] = { 40.f, 100.f, 250.f, 625.f, 1600.f, 4000.f, 10000.f };

static const char* const kTMaxNames[kParamCount] = {
    "Tube Pre", "Tube Post", "Solid State", "Shelving Low", "Shelving High", "Balance", "X-Over", "Master",
    "40 Hz", "100 Hz", "250 Hz", "625 Hz", "1.6 kHz", "4 kHz", "10 kHz",
    "Active", "Channel Sel", "Combine", "Graphic In"
};
static const char* const kTMaxSymbols[kParamCount] = {
    "tubepre", "tubepost", "solid", "shelflow", "shelfhigh", "balance", "xover", "master",
    "eq40", "eq100", "eq250", "eq625", "eq1k6", "eq4k", "eq10k",
    "active", "chansel", "combine", "graphicin"
};
static const float kTMaxMin[kParamCount] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0 };
static const float kTMaxMax[kParamCount] = { 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1, 1,1,1,1 };
// Tube Pre/Solid 0.5; Tube Post 0.6; shelves flat 0.5; Balance 0.5 (centre);
// X-Over 0.5 (~316 Hz); Master 0.7; EQ flat 0.5; Active off (passive);
// Channel Sel 1 (Tube); Combine on (both channels); Graphic In on.
static const float kTMaxDef[kParamCount] = {
    0.50f, 0.60f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 1.00f, 1.00f, 1.00f
};

#endif // TMAX_PARAMS_H
