#ifndef CHORUS_ENSEMBLE_PARAMS_H
#define CHORUS_ENSEMBLE_PARAMS_H

/*
 * CHORUS ENSEMBLE — Boss CE-1 Chorus Ensemble (1976). Parody brand (no "Boss"
 * on the face). Reference: pedals/Boss CE-1/BOSS-CE1_ServiceNotes.pdf.
 *
 * The first Boss pedal: a TA7136P preamp -> MN3002 BBD (512 stages, clock
 * 60-200 kHz) with an LFO modulating the clock, a Q10 output low-pass to kill
 * clock leakage, a Noise-Killer gate, and a STEREO "Ensemble" output (the wide
 * chorus spread: one out = dry+wet, the other = dry-wet).
 *
 * Two modes off the panel switch:
 *   CHORUS  — triangle LFO (slow, 2.4 s..325 ms sweep), dry+wet mix; INTENSITY
 *             sets the modulation. Wide stereo.
 *   VIBRATO — sine LFO (325..90 ms = ~3..11 Hz), 100% wet (pitch mod only);
 *             DEPTH + RATE knobs.
 *
 * EXTRA gear (not mapped to any RS song). Panel = the real CE-1.
 */
enum ChorusEnsembleParamId
{
    kLevel = 0,   // LEVEL (VR1)
    kIntensity,   // CHORUS INTENSITY (VR12) — chorus mode
    kDepth,       // VIBRATO DEPTH (VR13) — vibrato mode
    kRate,        // VIBRATO RATE (VR14) — vibrato mode
    kMode,        // Chorus/Vibrato footswitch: 0 = Chorus, 1 = Vibrato
    kEffect,      // Normal/Effect footswitch: 0 = Normal (preamp only), 1 = Effect on
    kInputSens,   // HIGH/LOW slide (S1): 0 = Low (clean), 1 = High (hotter preamp)
    kParamCount
};

static const char* const kChorusEnsembleNames[kParamCount] = {
    "Level", "Intensity", "Depth", "Rate", "Mode", "Effect", "Input Sens",
};
static const char* const kChorusEnsembleSymbols[kParamCount] = {
    "level", "intensity", "depth", "rate", "mode", "effect", "input_sens",
};
static const float kChorusEnsembleMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kChorusEnsembleMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
// Defaults: the classic lush CE-1 chorus — Level unity, Intensity ~2 o'clock,
// vibrato knobs at musical mid, Chorus mode, Effect ON, input sensitivity LOW
// (instrument level). Effect = 0 = Normal passes the preamp-only signal, and
// Input Sens = 1 = High drives the preamp harder (the CE-1 preamp boost).
static const float kChorusEnsembleDef[kParamCount] = {
    0.60f, 0.62f, 0.50f, 0.45f, 0.00f, 1.00f, 0.00f,
};

#endif // CHORUS_ENSEMBLE_PARAMS_H
