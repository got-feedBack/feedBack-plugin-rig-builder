#ifndef PLEXI_PARAMS_H
#define PLEXI_PARAMS_H

/*
 * MARSTEN PLEXI = Marshall 1959 Super Lead 100W (Plexi / JMP) — the FULL front
 * panel, 1:1, from the local schematic (1959-01-60-02.pdf / 1959SLP). Parody
 * brand "Marsten" (same as the DSL100 / GM-2 / UV-1). The face must never read
 * "Marshall".
 *
 * The 1959 is a NON-MASTER-VOLUME amp: the two Loudness pots ARE the gain. Its
 * lineage is the Bassman 5F6-A -> JTM45 -> Super Lead, so it shares the FMV/TMB
 * tone stack but with the Marshall values (33K slope, 500pF treble cap) and a
 * hotter, brighter, EL34-grind voice.
 *
 * Signal path: 4 inputs (2 per channel, High/Low) -> V1 gain stages (the HIGH
 * TREBLE/"bright" channel has a 5000pF bright cap across Loudness I; the NORMAL
 * channel is darker) -> the two Loudness pots mix (the classic "jumpered" plexi
 * tone is both channels up) -> V2 recovery + cathode follower -> Marshall tone
 * stack (Treble/Bass/Middle) -> V3 long-tail PI -> 4x EL34 (~100W) -> output
 * transformer. PRESENCE taps the power-amp negative-feedback loop.
 *
 * Panel (1:1, left->right): PRESENCE, BASS, MIDDLE, TREBLE, LOUDNESS I (High
 * Treble channel), LOUDNESS II (Normal channel) + Power/Standby toggles.
 *
 * the game mapping (rs_knob_to_vst_param.json): the Plexi has no gain knob, so
 * RS Gain -> Loudness I (drives clean->crunch->roar, matching the gain_variants
 * G3/G5/G10 split). Treble/Bass/Mid -> tone stack, Pres -> Presence. Loudness II
 * sits at a musical blend via _static (the jumpered voice) and stays editable.
 */
enum PlexiParamId
{
    kPresence = 0,   // PRESENCE (power-amp NFB high-shelf)   [RS Pres]
    kBass,           // BASS  tone stack (1M)                 [RS Bass]
    kMiddle,         // MIDDLE tone stack (25K)               [RS Mid]
    kTreble,         // TREBLE tone stack (250K, 500pF)       [RS Treble]
    kLoudness1,      // LOUDNESS I  — High Treble/bright ch    [RS Loudness1]
    kLoudness2,      // LOUDNESS II — Normal channel           [RS Loudness2]
    kInput,          // input cable: Bright(0) / Both-jumpered(0.5) / Normal(1)
    kParamCount
};

static const char* const kPlexiNames[kParamCount] = {
    "Presence", "Bass", "Middle", "Treble", "Loudness I", "Loudness II", "Input",
};

static const char* const kPlexiSymbols[kParamCount] = {
    "presence", "bass", "middle", "treble", "loudness1", "loudness2", "input",
};

static const float kPlexiMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kPlexiMax[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: the classic cranked-plexi crunch off Loudness I, tone
// stack centred-ish, Loudness II down, input JUMPERED (Both) so raising Loudness
// II by hand immediately blends the Normal channel (the famous fat plexi tone).
static const float kPlexiDef[kParamCount] = {
    0.50f, 0.50f, 0.55f, 0.62f, 0.62f, 0.00f, 0.50f,
};

#endif // PLEXI_PARAMS_H
