#ifndef TW40_PARAMS_H
#define TW40_PARAMS_H

/*
 * BENDER BASSMAN = Fender Bassman 5F6-A tweed — the FULL front panel, 1:1, from
 * the local schematic (Fender_bassman_5f6a.pdf). Parody brand "Bender" (same as
 * the SuperNova 22 / Deluxe). The face must never read "Fender".
 *
 * The 5F6-A has SIX knobs and two jumperable channels (the amp that became the
 * JTM45). 12AY7 input -> per-channel volume -> 12AX7 -> FMV tone stack -> 12AX7
 * driver / long-tail PI -> 2x 5881 (~45W) -> GZ34 rectifier -> 4x10:
 *   PRESENCE (5K), MIDDLE (25K), BASS (1M), TREBLE (250K),
 *   VOL.BRIGHT (1M, with the 100pF bright cap), VOL.NORMAL (1M)
 * Inputs: a clickable cable selects BRIGHT / BOTH(jumpered) / NORMAL.
 *
 * the game mapping (rs_knob_to_vst_param.json): the 5F6-A has no "gain" knob —
 * the volumes are the gain. RS Gain -> Bright Volume (drives the breakup);
 * Treble/Bass/Mid -> the FMV tone stack, Pres -> Presence. The input sits at
 * BOTH (jumpered, the signature Bassman tone) with Normal Vol at a musical
 * blend via _static, and everything stays editable by hand.
 */
enum TW40ParamId
{
    kInput = 0,      // input cable: Bright(0) / Both-jumpered(0.5) / Normal(1)
    kBrightVol,      // BRIGHT channel Volume (1M, 100pF bright cap)  [RS Gain]
    kNormalVol,      // NORMAL channel Volume (1M)
    kTreble,         // FMV tone stack Treble (250K)                  [RS Treble]
    kBass,           // FMV tone stack Bass (1M)                      [RS Bass]
    kMiddle,         // FMV tone stack Middle (25K)                   [RS Mid]
    kPresence,       // Presence (5K, power-amp NFB)                  [RS Pres]
    kParamCount
};

static const char* const kTW40Names[kParamCount] = {
    "Input", "Bright Vol", "Normal Vol", "Treble", "Bass", "Middle", "Presence",
};

static const char* const kTW40Symbols[kParamCount] = {
    "input", "brightvol", "normalvol", "treble", "bass", "middle", "presence",
};

static const float kTW40Min[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kTW40Max[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: jumpered (Both) input — the classic 5F6-A tone — with
// Bright a touch past noon, Normal as the blend, FMV stack centred-ish.
static const float kTW40Def[kParamCount] = {
    0.50f, 0.58f, 0.42f, 0.60f, 0.50f, 0.55f, 0.45f,
};

#endif // TW40_PARAMS_H
