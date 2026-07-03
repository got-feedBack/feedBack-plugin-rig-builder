#ifndef FK800_PARAMS_H
#define FK800_PARAMS_H

// "Freddy Krueger 800BR" parameter set — a 1:1 map of the Gallien-Krueger 800RB
// front panel, taken from the service manual (sheet 406-0045 "Bob Gallien 800
// RB Preamp" + the Operators Manual block diagram / specs):
//
//   Signal flow:  Input(+ -10dB pad) -> Volume(preamp drive/growl)
//                 -> Voicing Filters (Lo Cut, Mid Contour, Hi Boost)
//                 -> 4-band Active EQ -> Boost -> Electronic Crossover
//                 -> Master Volumes (300W low / 100W high).
//
//   Active EQ (boost/cut, flat at "5" = 0.5):
//       Bass    low shelf  60 Hz      (manual: "boost/cut at 60Hz")
//       Lo-Mid  peak      250 Hz      (manual: "boost/cut at 250Hz")
//       Hi-Mid  peak     1.0 kHz      (manual: "boost/cut at 1kHz")
//       Treble  high shelf 4 kHz      (manual: "boost/cut at 4kHz")
//   Voicing Filters (the 3 square switches):
//       Lo Cut       bass roll-off (stage-rumble high-pass)
//       Mid Contour  notch at ~500 Hz ("mellow round sound")
//       Hi Boost     presence/edge boost ("adds edge and definition")
//   Input pad   -10 dB  (sens 2mV->6mV, headroom 1V->3V rms)
//   Boost       footswitchable preset, +15 dB max, switch-to-ground
//   Crossover   100 Hz .. 1.04 kHz, Full / Bi-Amp (default 500 Hz = "5")
//   Masters     300W Amp (low band) + 100W Amp (high band)

enum Fk800ParamId {
    // knobs
    kVolume = 0, kTreble, kHiMid, kLoMid, kBass,
    kBoostLevel, kXover, kMaster100, kMaster300,
    // switches
    kPad, kLoCut, kContour, kHiBoost, kBoostOn, kBiamp,
    kParamCount
};

static const char* const kFk800Names[kParamCount] = {
    "Volume", "Treble", "Hi-Mid", "Lo-Mid", "Bass",
    "Boost", "Crossover", "100W Amp", "300W Amp",
    "-10dB", "Lo Cut", "Mid Contour", "Hi Boost", "Boost On", "Bi-Amp"
};
static const char* const kFk800Symbols[kParamCount] = {
    "volume", "treble", "himid", "lomid", "bass",
    "boost", "crossover", "master100", "master300",
    "pad", "locut", "contour", "hiboost", "booston", "biamp"
};

static const float kFk800Min[kParamCount] = { 0,0,0,0,0, 0,0,0,0, 0,0,0,0,0,0 };
static const float kFk800Max[kParamCount] = { 1,1,1,1,1, 1,1,1,1, 1,1,1,1,1,1 };
// Tone knobs flat at 0.5; Volume 0.7; Boost 0.5; Crossover 0.5 (=500Hz);
// Masters 0.8 (~ unity-ish, "cleanest near 10"); all switches off.
static const float kFk800Def[kParamCount] = {
    0.70f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.50f, 0.50f, 0.80f, 0.80f,
    0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f
};

#endif // FK800_PARAMS_H
