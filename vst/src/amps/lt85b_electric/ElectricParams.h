#ifndef ELECTRIC_PARAMS_H
#define ELECTRIC_PARAMS_H

// "Electric B600F" — Acoustic B600H (600 W solid-state bass head) front panel:
//   Passive / Active inputs + Mute.
//   PREAMP : Gain (+ Clip) and Volume (master).
//   NOTCH  : a sweepable notch filter — Frequency + On/Off.
//   EQUALIZER : a 6-band tone EQ — 40 / 120 / 350 / 800 / 2k / 5k Hz, +/-15 dB.
enum ElectricParamId {
    kGain = 0, kVolume, kNotchFreq, kEq40, kEq120, kEq350, kEq800, kEq2k, kEq5k,  // knobs
    kActive, kMute, kNotchOn,                                                      // switches
    kParamCount
};
static const int kFirstEq = kEq40;
static const int kNumEq = 6;
static const float kEqFreqs[kNumEq] = { 40.f, 120.f, 350.f, 800.f, 2000.f, 5000.f };

static const char* const kElectricNames[kParamCount] = {
    "Gain", "Volume", "Notch Freq", "40 Hz", "120 Hz", "350 Hz", "800 Hz", "2 kHz", "5 kHz",
    "Active", "Mute", "Notch"
};
static const char* const kElectricSymbols[kParamCount] = {
    "gain", "volume", "notchfreq", "eq40", "eq120", "eq350", "eq800", "eq2k", "eq5k",
    "active", "mute", "notchon"
};
static const float kElectricMin[kParamCount] = { 0,0,0,0,0,0,0,0,0, 0,0,0 };
static const float kElectricMax[kParamCount] = { 1,1,1,1,1,1,1,1,1, 1,1,1 };
// Gain 0.5; Volume 0.7; Notch Freq 0.5; EQ flat 0.5; Active/Mute/Notch off.
static const float kElectricDef[kParamCount] = {
    0.50f, 0.70f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
    0.00f, 0.00f, 0.00f
};

#endif // ELECTRIC_PARAMS_H
