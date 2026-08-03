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
 * treble bright cap) — summing into a shared tone stack (Bass A500K, Treble L250K,
 * Middle L100K), an L250K MASTER VOLUME, then the fixed-bias EL34 pair. PRESENCE
 * is L100K in the NFB loop. It shares the DR103 preamp, but the two-tube power
 * stage and 1K screen resistors compress and break up somewhat earlier.
 *
 * Panel (1:1, left->right): NORMAL VOL, BRILLIANT VOL, BASS, TREBLE, MIDDLE,
 * PRESENCE, MASTER VOL + 4 inputs (Normal Hi/Lo, Brilliant Hi/Lo) + STANDBY/MAINS.
 *
 * the game mapping (rs_knob_to_vst_param.json): no gain knob, so RS Gain ->
 * BRILLIANT VOL (the bright channel volume = the breakup driver); Bass/Mid/Treble
 * -> tone stack, Pres -> Presence. Input pinned to BOTH (jumpered) with Normal
 * Vol + Master at musical defaults via _static; all editable by hand.
 * CAB SIM is audition-only. Amp-only operation and external cab/IR chains use 0.
 */
enum Dr504ParamId
{
    kNormalVol = 0,  // NORMAL channel volume (A500K)
    kBrightVol,      // BRILLIANT channel volume (A500K, 1n coupling) [RS Gain]
    kBass,           // BASS  tone stack (A500K)                       [RS Bass]
    kTreble,         // TREBLE tone stack (L250K)                      [RS Treble]
    kMiddle,         // MIDDLE tone stack (L100K, the Hiwatt mids)     [RS Mid]
    kPresence,       // PRESENCE (L100K, power-amp NFB)                [RS Pres]
    kMaster,         // MASTER VOLUME (L250K)
    kInput,          // input cable: Normal(0) / Both-jumpered(0.5) / Brilliant(1)
    kCabSim,         // temporary internal Fane 4x12 cab filter       [host]
    kParamCount
};

static const char* const kDr504Names[kParamCount] = {
    "Normal Vol", "Brilliant Vol", "Bass", "Treble", "Middle", "Presence",
    "Master Vol", "Input", "Cab Sim",
};

static const char* const kDr504Symbols[kParamCount] = {
    "normalvol", "brilliantvol", "bass", "treble", "middle", "presence",
    "mastervol", "input", "cab_sim",
};

static const float kDr504Min[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kDr504Max[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: jumpered (Both), the brilliant channel up, the strong
// Hiwatt mids, Master past noon for that clean-loud punch. Tweak the volumes /
// switch the input cable by hand.
static const float kDr504Def[kParamCount] = {
    0.50f, 0.55f, 0.50f, 0.50f, 0.50f, 0.50f, 0.60f, 0.50f, 0.00f,
};

#endif // DR504_PARAMS_H
