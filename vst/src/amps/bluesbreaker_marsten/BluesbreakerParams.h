#ifndef BLUESBREAKER_PARAMS_H
#define BLUESBREAKER_PARAMS_H

/*
 * MARSTEN BLUESBREAKER = Marshall 1962 Bluesbreaker combo — the FULL front
 * panel, 1:1, from the local schematic (Marshall_bluesbreaker_reissue_45w_1962
 * .pdf, "1962 Reissue Valve Tremolo Combo"). Parody brand "Marsten" (same as
 * the Plexi / DSL100 / JCM800). The face must never read "Marshall".
 *
 * The 1962 is a JTM45-voiced NON-MASTER combo: the two Loudness (Volume I/II)
 * pots ARE the gain. Lineage: Bassman 5F6-A -> JTM45 -> 1962. It shares the
 * Marshall FMV/TMB tone stack but with the JTM45 values (56K slope, 220pF
 * treble cap, Treble 220K, Bass 1M, Mid 22K). Power amp: 2x 5881/KT66 (~30W)
 * + GZ34 rectifier (warm, sag-y). The 1962 ADDS a power-amp TREMOLO (V6 phase-
 * shift LFO + J174 FET modulator on the output): SPEED = rate, INTENSITY =
 * depth (0 = off).
 *
 * Signal path: Ch I / Ch II inputs -> V1 (ECC83) gain stages, bright caps
 * across Volume I/II -> the two Loudness pots mix (jumpered tone is both up)
 * -> V2 recovery + cathode follower -> Marshall tone stack -> V3 driver ->
 * long-tail PI -> 2x 5881/KT66 (~30W) + GZ34 sag -> output transformer.
 * PRESENCE (VR6 4k7) taps the power-amp NFB (R25 27K to the 16ohm tap). The
 * TREMOLO LFO (V6 / VR8 1M Speed / VR7 220K Intensity) amplitude-modulates the
 * power-amp output.
 *
 * Panel (1:1, left->right): SPEED, INTENSITY, PRESENCE, BASS, MIDDLE, TREBLE,
 * LOUDNESS 1 (Volume I), LOUDNESS 2 (Volume II), INPUT (cable/channel).
 *
 * the game mapping (rs_knob_to_vst_param.json): the 1962 has no gain knob, so
 * RS Gain -> LOUDNESS 1 (clean->crunch->roar). Treble/Bass/Mid -> tone stack,
 * Pres -> Presence. Tremolo OFF by default (Intensity 0); Speed/Intensity stay
 * editable by hand. Loudness 2 sits at a musical blend via _static.
 */
enum BluesbreakerParamId
{
    kSpeed = 0,      // SPEED      — tremolo LFO rate (VR8 1M)        [editable]
    kIntensity,      // INTENSITY  — tremolo depth (VR7 220K), 0=off  [editable]
    kPresence,       // PRESENCE   — power-amp NFB high-shelf (VR6)    [RS Pres]
    kBass,           // BASS  tone stack (1M)                         [RS Bass]
    kMiddle,         // MIDDLE tone stack (22K)                       [RS Mid]
    kTreble,         // TREBLE tone stack (220K, 220pF)               [RS Treble]
    kLoudness1,      // LOUDNESS 1 — Volume I (bright/lead channel)   [RS Gain]
    kLoudness2,      // LOUDNESS 2 — Volume II (normal channel)       [RS Loud2]
    kInput,          // input cable: Ch I(0) / Both-jumpered(0.5) / Ch II(1)
    kParamCount
};

static const char* const kBluesbreakerNames[kParamCount] = {
    "Speed", "Intensity", "Presence", "Bass", "Middle", "Treble",
    "Loudness 1", "Loudness 2", "Input",
};

static const char* const kBluesbreakerSymbols[kParamCount] = {
    "speed", "intensity", "presence", "bass", "middle", "treble",
    "loudness1", "loudness2", "input",
};

static const float kBluesbreakerMin[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kBluesbreakerMax[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults (per amp spec): tremolo OFF (Intensity 0), Speed mid,
// the classic bluesy bluesbreaker edge-of-breakup off Loudness 1, tone stack
// centred-ish (Treble a touch up), Loudness 2 down, input JUMPERED (Both) so
// raising Loudness 2 by hand blends the second channel (the fat 1962 tone).
static const float kBluesbreakerDef[kParamCount] = {
    0.50f, 0.00f, 0.50f, 0.50f, 0.50f, 0.60f, 0.60f, 0.30f, 0.50f,
};

#endif // BLUESBREAKER_PARAMS_H
