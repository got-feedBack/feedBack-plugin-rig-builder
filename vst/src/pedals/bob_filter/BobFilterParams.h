#ifndef BOB_FILTER_PARAMS_H
#define BOB_FILTER_PARAMS_H

// Bob Filter — Moog MF-101 Lowpass Filter (the Moog ENVELOPE filter).
// The game's gear exposes Sens/Attack/Release/Mix/Filter — an envelope-filter
// panel — so the model is the MF-101: envelope follower sweeping a 4-pole
// transistor-ladder LPF. (The previous model here was the MF-105 MuRF pattern
// sequencer, a different pedal entirely — it stepped a rhythm instead of
// tracking the pick, which is why it never behaved as an envelope filter.)
//
// Real MF-101S panel: Drive, Output, Cutoff, Resonance, bipolar Envelope
// Amount, Follow Rate, Mix and the 2-pole/4-pole switch. Rocksmith's separate
// Attack/Release controls are translated onto Follow Rate in the mapping table;
// they are not extra controls on the pedal face.
enum BobFilterParamId
{
    kDrive = 0,
    kOutput,
    kCutoff,
    kResonance,
    kEnvelope,      // -10..+10, stored normalized with zero at 0.5
    kFollowRate,    // slow..fast
    kMix,
    kPoles,         // 2-pole (0) / 4-pole (1)
    kParamCount
};

static const char* const kBobFilterNames[kParamCount] = {
    "Drive",
    "Output",
    "Cutoff",
    "Resonance",
    "Amount",
    "Follow Rate",
    "Mix",
    "Poles",
};

static const char* const kBobFilterSymbols[kParamCount] = {
    "drive",
    "output",
    "cutoff",
    "resonance",
    "amount",
    "follow_rate",
    "mix",
    "poles",
};

static const float kBobFilterMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBobFilterMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBobFilterDef[kParamCount] = {
    0.60f,   // Drive (about 1 o'clock)
    0.75f,   // Output
    0.612f,  // Cutoff = 1 kHz on the real 20 Hz..12 kHz scale
    0.70f,   // Resonance
    1.00f,   // Amount = +10
    1.00f,   // Follow Rate = fast
    1.00f,   // Mix (MF-101 is full wet; the game maps Mix)
    1.00f,   // 4-pole
};

#endif // BOB_FILTER_PARAMS_H
