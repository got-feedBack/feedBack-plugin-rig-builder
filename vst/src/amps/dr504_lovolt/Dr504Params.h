#ifndef DR504_PARAMS_H
#define DR504_PARAMS_H

/*
 * LOVOLT DR504 = Hiwatt DR504 "Custom Hiwatt 50" — the FULL front panel, 1:1,
 * from the local layout/schematic (DR504_Complete.pdf, Mark Huss). Parody brand
 * "Lovolt" (Hiwatt = high watt -> Lovolt = low volt; same brand as the Lovolt
 * 100). The face must never read "Hiwatt".
 *
 * A high-headroom, clean-and-loud EL34 amp (3x ECC83 + ECC81 PI + 2x EL34 ~50W):
 * two jumperable channels — NORMAL and BRILLIANT (the bright channel runs a
 * treble bright cap) — summing into a shared tone stack (Bass 500K, Treble 250K,
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
 */
enum Dr504ParamId
{
    kNormalVol = 0,  // NORMAL channel volume (500K)
    kBrightVol,      // BRILLIANT channel volume (500K, bright cap)  [RS Gain]
    kBass,           // BASS  tone stack (500K)                      [RS Bass]
    kTreble,         // TREBLE tone stack (250K)                     [RS Treble]
    kMiddle,         // MIDDLE tone stack (100K, the Hiwatt mids)    [RS Mid]
    kPresence,       // PRESENCE (100K, power-amp NFB)               [RS Pres]
    kMaster,         // MASTER VOLUME (250K)
    kInput,          // input cable: Normal(0) / Both-jumpered(0.5) / Brilliant(1)
    kParamCount
};

static const char* const kDr504Names[kParamCount] = {
    "Normal Vol", "Brilliant Vol", "Bass", "Treble", "Middle", "Presence",
    "Master Vol", "Input",
};

static const char* const kDr504Symbols[kParamCount] = {
    "normalvol", "brilliantvol", "bass", "treble", "middle", "presence",
    "mastervol", "input",
};

static const float kDr504Min[kParamCount] = { 0,0,0,0,0,0,0,0 };
static const float kDr504Max[kParamCount] = { 1,1,1,1,1,1,1,1 };
// Manual-insert defaults: jumpered (Both), the brilliant channel up, the strong
// Hiwatt mids, Master past noon for that clean-loud punch. Tweak the volumes /
// switch the input cable by hand.
static const float kDr504Def[kParamCount] = {
    0.50f, 0.55f, 0.50f, 0.60f, 0.60f, 0.50f, 0.55f, 0.50f,
};

#endif // DR504_PARAMS_H
