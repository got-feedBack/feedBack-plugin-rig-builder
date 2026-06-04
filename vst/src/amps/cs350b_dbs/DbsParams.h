#ifndef DBS_PARAMS_H
#define DBS_PARAMS_H

// "Marsten DBS 7400" — full Marshall DBS 7400 (Dynamic Bass System) front panel,
// modeled 1:1 from the 7400 schematic + panel. EVERY real control is a working
// knob/switch (even the ones Rocksmith doesn't drive — those just sit at their
// default and the player can still move them):
//   Gain          : op-amp preamp gain (clean SS — no tube saturation).
//   Pre-amp Blend : blends the VALVE voicing (warm, soft) <-> SOLID-STATE (clean).
//   Bright / Deep : voicing switches (HF lift / LF lift).
//   Bass/Middle/Treble : the passive Primary EQ (+/-15 dB).
//   Threshold     : compressor threshold (ON .. MIN .. MAX).
//   Depth         : compressor depth/amount.
//   7-band graphic EQ : 30/90/275/750/2.2k/6.5k/12k Hz (+/-15 dB) — the RS bands.
//   Graphic Level : graphic-EQ make-up (+/-6 dB).
//   Graphic       : graphic-EQ in/out switch.
//   Volume        : master into the SS power amp (~400 W).
//   Lo Input      : the padded (Lo) input jack.
// Rocksmith ("CLH-350B") drives Gain, Bass, Treble and the 7 graphic bands.
enum DbsParamId {
    kGain = 0, kBlend, kBass, kMiddle, kTreble, kThreshold, kDepth, kVolume,  // knobs
    kEq30, kEq90, kEq275, kEq750, kEq2k2, kEq6k5, kEq12k, kGraphicLevel,      // graphic faders
    kBright, kDeep, kGraphicOn, kLoInput,                                     // switches
    kParamCount
};
static const int kFirstEq = kEq30;     // 7 EQ bands are contiguous from here
static const int kNumEq = 7;
static const float kEqFreqs[kNumEq] = { 30.f, 90.f, 275.f, 750.f, 2200.f, 6500.f, 12000.f };

static const char* const kDbsNames[kParamCount] = {
    "Gain", "Pre-amp Blend", "Bass", "Middle", "Treble", "Threshold", "Depth", "Volume",
    "30 Hz", "90 Hz", "275 Hz", "750 Hz", "2.2 kHz", "6.5 kHz", "12 kHz", "Graphic Level",
    "Bright", "Deep", "Graphic", "Lo Input"
};
static const char* const kDbsSymbols[kParamCount] = {
    "gain", "blend", "bass", "middle", "treble", "threshold", "depth", "volume",
    "eq30", "eq90", "eq275", "eq750", "eq2k2", "eq6k5", "eq12k", "graphiclevel",
    "bright", "deep", "graphic", "loinput"
};
static const float kDbsMin[kParamCount] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0 };
static const float kDbsMax[kParamCount] = { 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1 };
// Gain 0.30; Blend 0.60 (toward solid-state); tone 0.5 flat; Threshold 0.5;
// Depth 0.35; Volume 0.7; EQ bands + Graphic Level 0.5 (flat); Bright/Deep off;
// Graphic ON; Lo input off.
static const float kDbsDef[kParamCount] = {
    0.30f, 0.60f, 0.50f, 0.50f, 0.50f, 0.50f, 0.35f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 0.00f, 1.00f, 0.00f
};

#endif // DBS_PARAMS_H
