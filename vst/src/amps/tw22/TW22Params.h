#ifndef TW22_PARAMS_H
#define TW22_PARAMS_H

/*
 * BENDER SUPERNOVA 22 = Fender Super-Sonic 22 — the FULL front panel, 1:1, from
 * the local schematic (Fender-Super-Sonic-22-Schematic.pdf, sheet 3 = panel).
 *
 * The amp has TWO footswitchable channels. The VST exposes every real control:
 *   VINTAGE channel (blackface clean): Volume, Treble, Bass + Norm/Fat switch
 *   VINTAGE/BURN channel select
 *   BURN channel (cascaded hi-gain): Gain 1, Gain 2, Treble, Bass, Middle, Volume
 *   shared Reverb
 * Plus a hidden Presence (the power-amp NFB brightness — the SS22 has no front
 * presence pot, so it only exists for the game Pres knob).
 *
 * the game mapping (rs_knob_to_vst_param.json): the single Gain knob DRIVES THE
 * CHANNEL — low Gain morphs to the Vintage clean, high Gain to the cascaded Burn.
 *   Gain->Channel, Treble->Burn Treble, Bass->Burn Bass, Mid->Burn Middle,
 *   Bright->Norm/Fat, Pres->Presence. The Vintage knobs + Burn Gain/Vol sit at
 *   musical defaults (_static) for songs and stay editable by hand.
 */
enum TW22ParamId
{
    kVintVol = 0,   // VINTAGE Volume
    kVintTreble,    // VINTAGE Treble
    kVintBass,      // VINTAGE Bass
    kNormFat,       // Norm/Fat switch (Vintage voicing)   [RS Bright]
    kChannel,       // Vintage(0) .. Burn(1) select/morph  [RS Gain]
    kGain1,         // BURN Gain 1
    kGain2,         // BURN Gain 2
    kBurnTreble,    // BURN Treble                         [RS Treble]
    kBurnBass,      // BURN Bass                           [RS Bass]
    kBurnMid,       // BURN Middle                         [RS Mid]
    kBurnVol,       // BURN Volume
    kReverb,        // shared Reverb level
    kPresence,      // power-amp presence (hidden)         [RS Pres]
    kParamCount
};

static const char* const kTW22Names[kParamCount] = {
    "Vint Vol", "Vint Treble", "Vint Bass", "Norm/Fat", "Channel",
    "Gain 1", "Gain 2", "Treble", "Bass", "Middle", "Burn Vol",
    "Reverb", "Presence",
};

static const char* const kTW22Symbols[kParamCount] = {
    "vintvol", "vinttreble", "vintbass", "normfat", "channel",
    "gain1", "gain2", "treble", "bass", "middle", "burnvol",
    "reverb", "presence",
};

static const float kTW22Min[kParamCount] = { 0,0,0,0,0, 0,0,0,0,0,0, 0,0 };
static const float kTW22Max[kParamCount] = { 1,1,1,1,1, 1,1,1,1,1,1, 1,1 };
// Manual-insert defaults: Burn channel selected (the Super-Sonic signature) at a
// moderate rock gain, both channels' EQ centred-ish, reverb off, presence mid.
static const float kTW22Def[kParamCount] = {
    0.50f, 0.60f, 0.50f, 0.00f, 1.00f,
    0.60f, 0.50f, 0.60f, 0.50f, 0.55f, 0.50f,
    0.00f, 0.50f,
};

#endif // TW22_PARAMS_H
