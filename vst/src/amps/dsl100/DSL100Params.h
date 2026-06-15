#ifndef DSL100_PARAMS_H
#define DSL100_PARAMS_H

/*
 * MARSTEN DSL100 = Marshall JCM2000 DSL100(H) — the FULL front panel, 1:1, from
 * the local schematics (JCM2-60..64). Parody brand "Marsten" (same as the GM-2 /
 * UV-1 Marshall-copy pedals); the face must never read "Marshall".
 *
 * Two footswitchable channels off a shared 3x ECC83 preamp + EL34 power amp:
 *   CLASSIC GAIN : Clean / Crunch    — Gain, Volume + Clean/Crunch switch
 *   ULTRA GAIN   : OD1 / OD2          — Gain, Volume + OD1/OD2 switch
 *   shared EQ    : Bass, Middle, Treble + Tone Shift (mid-scoop switch)
 *   power amp    : Resonance (low NFB) + Presence (high NFB), Output Low/High
 *   reverb       : per-channel level (Classic / Ultra), dual Master (1/2 select)
 *
 * the game mapping (rs_knob_to_vst_param.json): the single Gain knob DRIVES THE
 * CHANNEL — low Gain = Classic clean, mid = Classic crunch, high = Ultra OD
 * (matching the gain_variants split). Treble/Mid/Bass -> tone stack,
 * Pres -> Presence, Res -> Resonance. The per-channel Gain/Vol + masters sit at
 * musical defaults (_static) and stay editable by hand.
 */
enum DSL100ParamId
{
    kChannel = 0,    // CLASSIC(0) / ULTRA(1) select       [RS Gain morph]
    kClassicGain,    // CLASSIC GAIN
    kClassicVol,     // CLASSIC VOLUME
    kClassicMode,    // Clean(0) / Crunch(1)
    kUltraGain,      // ULTRA GAIN
    kUltraVol,       // ULTRA VOLUME
    kUltraMode,      // OD1(0) / OD2(1)
    kBass,           // tone stack BASS                    [RS Bass]
    kMid,            // tone stack MIDDLE                  [RS Mid]
    kTreble,         // tone stack TREBLE                  [RS Treble]
    kToneShift,      // Tone Shift mid-scoop  Off(0)/On(1)
    kResonance,      // power-amp low-end NFB (Deep)       [RS Res]
    kPresence,       // power-amp high-end NFB             [RS Pres]
    kRevClassic,     // CLASSIC reverb level
    kRevUltra,       // ULTRA reverb level
    kMaster1,        // MASTER 1
    kMaster2,        // MASTER 2
    kMasterSelect,   // Master 1(0) / Master 2(1)
    kOutput,         // Output  Low/50W(0) / High/100W(1)
    kParamCount
};

static const char* const kDSL100Names[kParamCount] = {
    "Channel", "Classic Gain", "Classic Vol", "Classic Mode",
    "Ultra Gain", "Ultra Vol", "Ultra Mode",
    "Bass", "Middle", "Treble", "Tone Shift",
    "Resonance", "Presence", "Rev Classic", "Rev Ultra",
    "Master 1", "Master 2", "Master Sel", "Output",
};

static const char* const kDSL100Symbols[kParamCount] = {
    "channel", "classicgain", "classicvol", "classicmode",
    "ultragain", "ultravol", "ultramode",
    "bass", "middle", "treble", "toneshift",
    "resonance", "presence", "revclassic", "revultra",
    "master1", "master2", "mastersel", "output",
};

static const float kDSL100Min[kParamCount] = {
    0,0,0,0, 0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 };
static const float kDSL100Max[kParamCount] = {
    1,1,1,1, 1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 };
// Manual-insert defaults: Ultra channel (the DSL lead voice) at OD1, moderate
// gain; Classic set to a usable crunch; tone stack centred-ish; reverb off;
// masters at unity-ish; High (100W) output.
static const float kDSL100Def[kParamCount] = {
    1.00f, 0.40f, 0.50f, 1.00f,   // Channel=Ultra, Classic Gain/Vol, Classic=Crunch
    0.60f, 0.50f, 0.00f,          // Ultra Gain/Vol, Ultra=OD1
    0.55f, 0.50f, 0.62f, 0.00f,   // Bass, Middle, Treble, Tone Shift off
    0.50f, 0.45f, 0.00f, 0.00f,   // Resonance, Presence, Rev Classic/Ultra off
    0.50f, 0.50f, 0.00f, 1.00f,   // Master 1/2, Master Sel=1, Output=High
};

#endif // DSL100_PARAMS_H
