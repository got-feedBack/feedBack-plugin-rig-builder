#ifndef BOB_FILTER_PARAMS_H
#define BOB_FILTER_PARAMS_H

// Bob Filter — Moog MF-101 Lowpass Filter (the Moog ENVELOPE filter).
// The game's gear exposes Sens/Attack/Release/Mix/Filter — an envelope-filter
// panel — so the model is the MF-101: envelope follower sweeping a 4-pole
// transistor-ladder LPF. (The previous model here was the MF-105 MuRF pattern
// sequencer, a different pedal entirely — it stepped a rhythm instead of
// tracking the pick, which is why it never behaved as an envelope filter.)
//
// Real MF-101 panel: Drive, Output, Cutoff, Resonance, Envelope Amount,
// Smooth/Fast switch, 2-pole/4-pole switch. The game gives CONTINUOUS
// Attack/Release, so those replace the Smooth/Fast switch.
enum BobFilterParamId
{
    kDrive = 0,
    kOutput,
    kCutoff,
    kResonance,
    kEnvelope,      // envelope AMOUNT (octaves of sweep)
    kAttack,
    kRelease,
    kMix,
    kMode,          // 2-pole (0) / 4-pole (1)
    kParamCount
};

static const char* const kBobFilterNames[kParamCount] = {
    "Drive",
    "Output",
    "Cutoff",
    "Resonance",
    "Envelope",
    "Attack",
    "Release",
    "Mix",
    "Mode",
};

static const char* const kBobFilterSymbols[kParamCount] = {
    "drive",
    "output",
    "cutoff",
    "resonance",
    "envelope",
    "attack",
    "release",
    "mix",
    "mode",
};

static const float kBobFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBobFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBobFilterDef[kParamCount] = {
    0.35f,   // Drive
    0.60f,   // Output
    0.22f,   // Cutoff (base ~160 Hz, mostly closed at rest)
    0.55f,   // Resonance (juicy quack, below self-osc)
    0.65f,   // Envelope amount
    0.15f,   // Attack (fast — the "Fast" switch feel)
    0.45f,   // Release
    1.00f,   // Mix (MF-101 is full wet; the game maps Mix)
    1.00f,   // Mode = 4-pole
};

#endif // BOB_FILTER_PARAMS_H
