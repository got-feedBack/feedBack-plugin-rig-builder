#ifndef DR103_PARAMS_H
#define DR103_PARAMS_H

/*
 * LOVOLT DR103 = Hiwatt DR103 "Custom Hiwatt 100" — the FULL front panel, 1:1,
 * from the local layout/schematic (DR103_Complete.pdf, Mark Huss). Parody brand
 * "Lovolt" (Hiwatt = high watt -> Lovolt = low volt; same brand as the Lovolt
 * 100). The face must never read "Hiwatt".
 *
 * A high-headroom, clean-and-loud EL34 amp (3x ECC83 + ECC81 PI + 4x EL34 ~100W):
 * two jumperable channels — NORMAL and BRILLIANT (the bright channel runs a
 * treble bright cap) — summing into a shared tone stack (Bass A470K, Treble L220K,
 * Middle 100K -> the strong Hiwatt mids), a MASTER VOLUME, then the EL34 power
 * amp. PRESENCE taps the power-amp NFB. The Hiwatt stays clean far longer than a
 * Plexi; breakup comes mostly from cranking the MASTER.
 *
 * Panel (1:1, left->right): NORMAL VOL, BRILLIANT VOL, BASS, TREBLE, MIDDLE,
 * PRESENCE, MASTER VOL + 4 inputs (Normal Hi/Lo, Brilliant Hi/Lo) + STANDBY/MAINS.
 *
 * the game mapping (rs_knob_to_vst_param.json): no gain knob, so RS Gain ->
 * BRILLIANT VOL (the bright channel volume = the breakup driver); Bass/Mid/Treble
 * -> tone stack, Pres -> Presence. Input pinned to BOTH (jumpered) with Normal
 * Vol + Master at musical defaults via _static; all editable by hand.
 * CAB SIM is an optional audition-only Fane-style fallback. Reference matching
 * and normal amp-only operation use 0; an external cabinet/IR remains separate.
 */
enum Dr103ParamId
{
    kNormalVol = 0,  // NORMAL channel volume (A470K)
    kBrightVol,      // BRILLIANT channel volume (A470K, 1n coupling) [RS Gain]
    kBass,           // BASS  tone stack (A470K)                      [RS Bass]
    kTreble,         // TREBLE tone stack (L220K)                     [RS Treble]
    kMiddle,         // MIDDLE tone stack (L100K, the Hiwatt mids)  [RS Mid]
    kPresence,       // PRESENCE (L100K, power-amp NFB)             [RS Pres]
    kMaster,         // MASTER VOLUME (A220K)
    kInput,          // input cable: Normal(0) / Both-jumpered(0.5) / Brilliant(1)
    kCabSim,         // temporary internal Fane 4x12 cab filter       [host]
    kParamCount
};

static const char* const kDr103Names[kParamCount] = {
    "Normal Vol", "Brilliant Vol", "Bass", "Treble", "Middle", "Presence",
    "Master Vol", "Input", "Cab Sim",
};

static const char* const kDr103Symbols[kParamCount] = {
    "normalvol", "brilliantvol", "bass", "treble", "middle", "presence",
    "mastervol", "input", "cab_sim",
};

static const float kDr103Min[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kDr103Max[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: jumpered (Both), the brilliant channel up, the strong
// Hiwatt mids, Master past noon for that clean-loud punch. Tweak the volumes /
// switch the input cable by hand.
static const float kDr103Def[kParamCount] = {
    0.50f, 0.55f, 0.50f, 0.50f, 0.50f, 0.50f, 0.60f, 0.50f, 0.00f,
};

#endif // DR103_PARAMS_H
