#ifndef REDHEAD_PARAMS_H
#define REDHEAD_PARAMS_H

// "SBR Super Redhead" — SWR Super Redhead (all-tube preamp / 350 W SS power)
// front panel, 1:1:
//   Gain    : 12AX7 input drive (Pull Turbo = hotter/grittier).
//   Aural   : the SWR "Aural Enhancer" contour (lifts lows + highs, scoops
//             low-mids) — 0 = off, up = more.
//   Bass    : low shelf (+/-15 dB).
//   Mid Level / Mid Freq : the semi-parametric midrange (level at a sweepable
//             centre, ~100 Hz .. 2 kHz).
//   Treble  : high shelf (+/-15 dB); Pull Transparency = extra high air.
//   Master  : output level into the 350 W solid-state power amp (clean hi-fi).
enum RedheadParamId {
    kGain = 0, kAural, kBass, kMidLevel, kMidFreq, kTreble, kMaster,  // knobs
    kActive, kTurbo, kTransparency,                                   // switches
    kParamCount
};

static const char* const kRedheadNames[kParamCount] = {
    "Gain", "Aural Enhancer", "Bass", "Mid Level", "Mid Freq", "Treble", "Master",
    "Active", "Turbo", "Transparency"
};
static const char* const kRedheadSymbols[kParamCount] = {
    "gain", "aural", "bass", "midlevel", "midfreq", "treble", "master",
    "active", "turbo", "transparency"
};
static const float kRedheadMin[kParamCount] = { 0,0,0,0,0,0,0, 0,0,0 };
static const float kRedheadMax[kParamCount] = { 1,1,1,1,1,1,1, 1,1,1 };
// Gain 0.5; Aural 0.3; Bass/Mid Level/Treble flat 0.5; Mid Freq 0.5 (centre);
// Master 0.7; switches off.
static const float kRedheadDef[kParamCount] = {
    0.50f, 0.30f, 0.50f, 0.50f, 0.50f, 0.50f, 0.70f,
    0.00f, 0.00f, 0.00f
};

#endif // REDHEAD_PARAMS_H
