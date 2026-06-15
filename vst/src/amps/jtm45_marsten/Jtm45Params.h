#ifndef JTM45_PARAMS_H
#define JTM45_PARAMS_H

/*
 * MARSTEN JTM45 = Marshall JTM45 (~30W, 2x KT66 + GZ34 tube rectifier) — the
 * FULL front panel, 1:1, from the local schematic (Marshall_jtm45_readable.pdf,
 * JTM45.DGM issue 7). Parody brand "Marsten" (same as the Plexi / DSL100); the
 * in-app face must never read "Marshall".
 *
 * The JTM45 is the predecessor of the 1959 Super Lead Plexi and the direct
 * descendant of the Fender Bassman 5F6-A: same jumper-input + dual Loudness
 * (non-master) topology and the Marshall/Bassman FMV tone stack. What differs
 * is the POWER amp and the tone-stack values:
 *   - 2x KT66 push-pull (~30W) with a GZ34 (5AR4) TUBE rectifier -> warmer,
 *     softer, MUCH more sag, earlier breakup and a darker top than the 100W
 *     4x EL34 Plexi.
 *   - Early JTM45/Bassman tone stack: Treble 250K / Bass 1M / Middle 25K with
 *     a 56K slope resistor and a 270pF treble cap (vs the Plexi's 33K / 500pF).
 *
 * Signal path (per schematic): 4 inputs (Ch.1 / Ch.2, High/Low) -> V1 gain
 * stages (the HIGH TREBLE/"bright" channel runs a 500pF bright cap across its
 * Loudness pot; the NORMAL channel is darker) -> the two Loudness pots mix (the
 * classic jumpered tone is both channels up) -> V2/V3 recovery + cathode
 * follower -> Marshall tone stack (Treble/Bass/Middle) -> long-tail PI -> 2x
 * KT66 (~30W) + GZ34 rectifier sag -> output transformer. PRESENCE (5K) taps
 * the power-amp negative-feedback loop.
 *
 * Panel (1:1, left->right): PRESENCE, BASS, MIDDLE, TREBLE, LOUDNESS 1 (High
 * Treble channel), LOUDNESS 2 (Normal channel) + Power/Standby toggles.
 *
 * the game mapping (rs_knob_to_vst_param.json): the JTM45 has no gain knob, so
 * RS Gain -> LOUDNESS 1 (the High-Treble channel volume = the breakup driver,
 * clean -> crunch -> roar). Treble/Bass/Mid -> tone stack, Pres -> Presence.
 * kInput = Bright(0) / Both-jumpered(0.5) / Normal(1), like the Plexi.
 */
enum Jtm45ParamId
{
    kPresence = 0,   // PRESENCE (power-amp NFB high-shelf, 5K pot)  [RS Pres]
    kBass,           // BASS  tone stack (1M)                        [RS Bass]
    kMiddle,         // MIDDLE tone stack (25K)                      [RS Mid]
    kTreble,         // TREBLE tone stack (250K, 270pF)              [RS Treble]
    kLoudness1,      // LOUDNESS 1 — High Treble/bright ch           [RS Loudness1]
    kLoudness2,      // LOUDNESS 2 — Normal channel                  [RS Loudness2]
    kInput,          // input cable: Bright(0) / Both-jumpered(0.5) / Normal(1)
    kParamCount
};

static const char* const kJtm45Names[kParamCount] = {
    "Presence", "Bass", "Middle", "Treble", "Loudness 1", "Loudness 2", "Input",
};

static const char* const kJtm45Symbols[kParamCount] = {
    "presence", "bass", "middle", "treble", "loudness1", "loudness2", "input",
};

static const float kJtm45Min[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kJtm45Max[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: the classic edge-of-breakup JTM45 off Loudness 1,
// tone stack centred-ish (treble a touch up to counter the darker KT66 top),
// Loudness 2 low, input JUMPERED (Both = 0.5) so raising Loudness 2 by hand
// immediately blends the Normal channel (the famous fat jumpered tone).
static const float kJtm45Def[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.60f, 0.60f, 0.30f, 0.50f,
};

#endif // JTM45_PARAMS_H
