#ifndef OR50_PARAMS_H
#define OR50_PARAMS_H

/*
 * CITRUS OR50 = Orange OR50 (vintage "Graphic" single-channel head) — the FULL
 * front panel, 1:1, from the local hand-drawn schematic (Orange100.pdf, "Orange
 * Model OR50 serial 94", Steven Poulsen — a COMPLETE single-page schematic:
 * input -> ECC83 gain stages -> tone stack + Depth (the bass-cap rotary) -> ECC81
 * PI -> 2x EL34 (~50W) + bias + FX loop). Parody brand "Citrus" (same as the
 * Citrus AD200). The face must never read "Orange".
 *
 * Panel (the famous Orange "pics-only" graphics): GAIN (HF Drive), BASS, MIDDLE
 * (FAC), TREBLE, DEPTH (low-end voicing), VOLUME (master) + a FULL/HALF output
 * power switch. A thick, midrange-forward British EL34 voice (the Orange "doom
 * chunk").
 *
 * the game (Amp_OrangeOR50, captured "V D T M B G"): RS Gain -> GAIN, Bass/Mid/
 * Treble -> tone stack. Volume + Depth pinned to musical defaults via _static;
 * all editable by hand (incl. the FULL/HALF switch).
 */
enum Or50ParamId
{
    kGain = 0,      // GAIN — HF Drive / preamp gain                 [RS Gain]
    kBass,          // BASS  tone stack                              [RS Bass]
    kMiddle,        // MIDDLE (FAC) tone stack                       [RS Mid]
    kTreble,        // TREBLE tone stack                             [RS Treble]
    kDepth,         // DEPTH — low-end voicing (the bass-cap rotary)
    kVolume,        // VOLUME — master volume (into the power amp)
    kHalf,          // output power: FULL(0) / HALF(1)
    kParamCount
};

static const char* const kOr50Names[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Depth", "Volume", "Half Power",
};

static const char* const kOr50Symbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "depth", "volume", "halfpower",
};

static const float kOr50Min[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kOr50Max[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: a thick Orange crunch — Gain past noon, the strong FAC
// mids, Depth + Volume around noon, FULL power.
static const float kOr50Def[kParamCount] = {
    0.55f, 0.50f, 0.55f, 0.55f, 0.50f, 0.55f, 0.0f,
};

#endif // OR50_PARAMS_H
