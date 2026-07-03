#ifndef DBS_PARAMS_H
#define DBS_PARAMS_H

// "Marsten DBS 7400" — Marshall DBS 7400 (Dynamic Bass System) front panel,
// modeled 1:1 from the 7400 service schematic (boards 7400-60-0A/0B) + panel.
// EVERY real control is a working knob/switch (the ones the game doesn't drive
// just sit at their default and the player can still move them):
//   Gain          : op-amp preamp gain (clean SS — no tube saturation). PEAK LED.
//   Pre-amp Blend : blends the VALVE voicing (warm, soft) <-> SOLID-STATE (clean).
//   Bright / Deep : voicing switches (HF lift / LF lift) — SW1 / SW2.
//   Lo / Hi       : the 2-band passive Primary EQ (VR3 / VR4, +/-15 dB). The real
//                   7400 has NO mid control — Primary EQ is Lo + Hi only.
//   Depth         : compressor depth/amount (VR5). Threshold is FIXED internally
//                   (SSM2252 VCA); the front-panel "Threshold" is just the
//                   INDICATOR LED that lights when the comp is acting — NOT a knob.
//   9-band graphic EQ : 50/80/160/320/640/1.25k/2.5k/5k/8k Hz (+/-15 dB) — the
//                   REAL 7400 bands (two M5227P, VR6..VR15).
//   Graphic Level : graphic-EQ make-up (+/-6 dB).
//   Graphic       : graphic-EQ in/out switch.
//   Volume        : master into the SS power amp (~400 W).
//   Lo Input      : the padded (Lo) input jack.
// the game ("CLH-350B") drives Gain, Bass->Lo, Treble->Hi and the graphic bands
// (RS sends 7 bands, remapped to the nearest of the real 9; see rs_knob_to_vst_param).
enum DbsParamId {
    kGain = 0, kBlend, kLo, kHi, kDepth, kVolume,                            // knobs
    kEq50, kEq80, kEq160, kEq320, kEq640, kEq1k25, kEq2k5, kEq5k, kEq8k, kGraphicLevel,  // graphic faders
    kBright, kDeep, kGraphicOn, kLoInput,                                    // switches
    kParamCount
};
static const int kFirstEq = kEq50;     // 9 EQ bands are contiguous from here
static const int kNumEq = 9;
static const float kEqFreqs[kNumEq] = { 50.f, 80.f, 160.f, 320.f, 640.f, 1250.f, 2500.f, 5000.f, 8000.f };

static const char* const kDbsNames[kParamCount] = {
    "Gain", "Pre-amp Blend", "Lo", "Hi", "Depth", "Volume",
    "50 Hz", "80 Hz", "160 Hz", "320 Hz", "640 Hz", "1.25 kHz", "2.5 kHz", "5 kHz", "8 kHz", "Graphic Level",
    "Bright", "Deep", "Graphic", "Lo Input"
};
static const char* const kDbsSymbols[kParamCount] = {
    "gain", "blend", "lo", "hi", "depth", "volume",
    "eq50", "eq80", "eq160", "eq320", "eq640", "eq1k25", "eq2k5", "eq5k", "eq8k", "graphiclevel",
    "bright", "deep", "graphic", "loinput"
};
static const float kDbsMin[kParamCount] = { 0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0 };
static const float kDbsMax[kParamCount] = { 1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1, 1,1,1,1 };
// Gain 0.30; Blend 0.60 (toward solid-state); Lo/Hi 0.5 flat; Depth 0.35;
// Volume 0.7; 9 EQ bands + Graphic Level 0.5 (flat); Bright/Deep off; Graphic ON;
// Lo input off.
static const float kDbsDef[kParamCount] = {
    0.30f, 0.60f, 0.50f, 0.50f, 0.35f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 0.00f, 1.00f, 0.00f
};

#endif // DBS_PARAMS_H
