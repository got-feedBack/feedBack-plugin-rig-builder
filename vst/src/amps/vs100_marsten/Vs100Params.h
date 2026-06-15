#ifndef VS100_PARAMS_H
#define VS100_PARAMS_H

/*
 * MARSTEN VS100 = Marshall Valvestate VS100RH — the FULL front panel, 1:1, from
 * the local schematic (v100-60/61/62-02.pdf). Parody brand "Marsten" (same as
 * the DSL100 / Plexi / DBS 7400). The face must never read "Marshall".
 *
 * A HYBRID head: solid-state op-amp preamp + a 12AX7 "Valvestate" stage feeding a
 * solid-state power amp (~100W). Three modes selected by the footswitch:
 *   - CLEAN  : Volume + its own Bass/Middle/Treble.
 *   - OD1    : a crunch overdrive (Gain + Volume), sharing the clean EQ.
 *   - OD2    : the high-gain lead — Gain + CONTOUR (mid scoop) + Volume + its own
 *              Bass/Middle/Treble (the Valvestate solid-state lead distortion).
 * Plus an FX MIX (loop blend) and per-mode reverb (Clean Reverb / OD Reverb).
 *
 * the game (Amp_HG180): the OD2 lead is the voice, so RS Gain -> OD2 Gain,
 * Bass/Mid/Treble -> OD2 Bass/Middle/Treble. Channel pinned to OD2 + reverb/FX
 * off via _static; everything editable by hand.
 */
enum Vs100ParamId
{
    kChannel = 0,    // Clean(0) / OD1(0.5) / OD2(1)
    kClVolume,       // CLEAN Volume
    kClBass,         // CLEAN Bass
    kClMid,          // CLEAN Middle
    kClTreble,       // CLEAN Treble
    kOd1Gain,        // OVERDRIVE 1 Gain
    kOd1Volume,      // OVERDRIVE 1 Volume
    kOd2Gain,        // OVERDRIVE 2 Gain                    [RS Gain]
    kOd2Contour,     // OVERDRIVE 2 Contour (mid scoop)
    kOd2Volume,      // OVERDRIVE 2 Volume
    kOd2Bass,        // OVERDRIVE 2 Bass                    [RS Bass]
    kOd2Mid,         // OVERDRIVE 2 Middle                  [RS Mid]
    kOd2Treble,      // OVERDRIVE 2 Treble                  [RS Treble]
    kFxMix,          // FX MIX (effects-loop blend)
    kCleanRev,       // CLEAN reverb
    kOdRev,          // OVERDRIVE reverb
    kParamCount
};

static const char* const kVs100Names[kParamCount] = {
    "Channel", "Cl Volume", "Cl Bass", "Cl Middle", "Cl Treble",
    "OD1 Gain", "OD1 Volume",
    "OD2 Gain", "OD2 Contour", "OD2 Volume", "OD2 Bass", "OD2 Middle", "OD2 Treble",
    "FX Mix", "Clean Reverb", "OD Reverb",
};

static const char* const kVs100Symbols[kParamCount] = {
    "channel", "clvolume", "clbass", "clmiddle", "cltreble",
    "od1gain", "od1volume",
    "od2gain", "od2contour", "od2volume", "od2bass", "od2middle", "od2treble",
    "fxmix", "cleanreverb", "odreverb",
};

static const float kVs100Min[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
static const float kVs100Max[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: OD2 lead mode, a singing Valvestate crunch/lead, EQ
// centred-ish, reverb + FX off. Clean + OD1 sit at clean/crunch defaults.
static const float kVs100Def[kParamCount] = {
    1.0f, 0.50f, 0.50f, 0.50f, 0.55f,
    0.50f, 0.50f,
    0.60f, 0.40f, 0.50f, 0.50f, 0.45f, 0.60f,
    0.00f, 0.00f, 0.00f,
};

#endif // VS100_PARAMS_H
