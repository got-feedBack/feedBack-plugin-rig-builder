#ifndef EDEN_WTDI_PARAMS_H
#define EDEN_WTDI_PARAMS_H

// the game "Eden WTDI" -> Eden World Tour Direct, a bass preamp/DI. Faithful
// to the real pedal (and the WT-400/WT-800 preamp it shares): an input Gain
// stage with gentle tube-ish drive, an optical-style Compressor, a 3-band
// active EQ (Bass / Mid / Treble, ±15 dB), and Eden's signature "Enhance"
// contour (boost lows+highs, scoop mids). The two red square switches on the
// face — "Bass Boost" and "Mid Shift" — are exposed by the game as the
// LoShift / MidShift knobs (0/1), so they're modeled as toggles: Bass Boost
// drops the low-shelf corner and adds bottom; Mid Shift moves the mid band
// from low-mid (~600 Hz) up to upper-mid (~1.2 kHz).
//
// the game knobs (8) -> params:
//   Bass        = Bass     (low shelf gain)
//   Mid         = Mid      (mid peak gain)
//   Treble      = Treble   (high shelf gain)
//   Gain        = Gain     (preamp drive)
//   Enhance     = Enhance  (contour amount)
//   Compression = Comp     (compressor amount)
//   LoShift     = Bass Boost (switch)
//   MidShift    = Mid Shift  (switch)
// "Master" is the pedal's output level — not a game-controlled knob, so
// it stays at its unity default and is only adjustable in the editor.
enum EdenWtdiParamId {
    kGain = 0, kEnhance, kComp, kMaster,
    kBass, kMid, kTreble,
    kBassBoost, kMidShift,
    kParamCount
};

static const char* const kEdenWtdiNames[kParamCount] = {
    "Gain", "Enhance", "Comp", "Master",
    "Bass", "Mid", "Treble",
    "Bass Boost", "Mid Shift"
};
static const char* const kEdenWtdiSymbols[kParamCount] = {
    "gain", "enhance", "comp", "master",
    "bass", "mid", "treble",
    "bassboost", "midshift"
};

static const float kEdenWtdiMin[kParamCount] = { 0,0,0,0, 0,0,0, 0,0 };
static const float kEdenWtdiMax[kParamCount] = { 1,1,1,1, 1,1,1, 1,1 };
// Bass/Mid/Treble default 0.5 = flat (0 dB). Master 0.7 ~ unity. Switches off.
static const float kEdenWtdiDef[kParamCount] = {
    0.50f, 0.00f, 0.00f, 0.70f,
    0.50f, 0.50f, 0.50f,
    0.00f, 0.00f
};

#endif // EDEN_WTDI_PARAMS_H
