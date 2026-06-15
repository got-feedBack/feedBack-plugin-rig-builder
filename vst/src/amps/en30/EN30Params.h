#ifndef EN30_PARAMS_H
#define EN30_PARAMS_H

/*
 * BOX DC30 = Vox AC30C2 (Custom series) — the FULL front panel, 1:1, modelled
 * from the local schematic (Vox_ac30c2.pdf: AC30C2 PreAmp / RevFX / Power Amp).
 *
 * The VST exposes the REAL amp controls plus two faithful extras that give the
 * the game Mid/Bright knobs a home WITHOUT inventing fake pots:
 *   - Input: which jack the cable is in — Normal(0) / Both jumpered(0.5) /
 *     Top Boost(1). The classic AC30 channel-jumpering trick. RS Mid rides this
 *     by turning up the Normal channel (its mid-forward voicing fills the Vox
 *     scoop), so Mid -> Normal Vol with Input pinned to Both.
 *   - Bright: the Top Boost brilliance bright-cap amount (a real preamp treble
 *     bypass, distinct from the power-amp Tone Cut). RS Bright -> this.
 *
 * the game mapping (rs_knob_to_vst_param.json): Gain->TB Vol, Treble->Treble,
 * Bass->Bass, Pres->Tone Cut(inv), Mid->Normal Vol(+Input=Both), Bright->Bright.
 *
 * Panel (AC30C2, left -> right), per the schematic pot designators:
 *   INPUTS (Normal Hi/Lo, Top Boost Hi/Lo; cable selectable) | NORMAL Volume
 *   (VR1 500K) | TOP BOOST Volume (VR2 A500K) / Treble (VR3 A1M) / Bass (VR4 A1M)
 *   | REVERB Tone (VR5 A500K) / Level (VR6 B100K) | TREMOLO Speed (VR7 2M2) /
 *   Depth (VR8 B500K) | MASTER Tone Cut (VR9 B220K) / Volume (VR10 A500K)
 *   | STANDBY | POWER
 */
enum EN30ParamId
{
    kNormalVol = 0,   // NORMAL Volume        VR1 500K      [RS Mid -> fills mids]
    kTBVol,           // TOP BOOST Volume     VR2 A500K     [RS Gain]
    kTreble,          // TOP BOOST Treble     VR3 A1M       [RS Treble]
    kBass,            // TOP BOOST Bass       VR4 A1M       [RS Bass]
    kRevTone,         // REVERB Tone          VR5 A500K
    kRevLevel,        // REVERB Level         VR6 B100K
    kSpeed,           // TREMOLO Speed        VR7 2M2 (C-taper)
    kDepth,           // TREMOLO Depth        VR8 B500K
    kCut,             // MASTER Tone Cut      VR9 B220K     [RS Pres, inverted]
    kMaster,          // MASTER Volume        VR10 A500K
    kInput,           // INPUT: Normal(0) / Both jumpered(0.5) / Top Boost(1)
    kBright,          // Top Boost bright-cap amount        [RS Bright]
    kParamCount
};

static const char* const kEN30Names[kParamCount] = {
    "Normal Vol", "TB Vol", "Treble", "Bass",
    "Rev Tone", "Rev Level", "Speed", "Depth",
    "Tone Cut", "Master", "Input", "Bright",
};

static const char* const kEN30Symbols[kParamCount] = {
    "normalvol", "tbvol", "treble", "bass",
    "revtone", "revlevel", "speed", "depth",
    "tonecut", "master", "input", "bright",
};

static const float kEN30Min[kParamCount] = { 0,0,0,0, 0,0,0,0, 0,0, 0,0 };
static const float kEN30Max[kParamCount] = { 1,1,1,1, 1,1,1,1, 1,1, 1,1 };
// Manual-insert defaults: Normal off, Top Boost at a moderate volume, reverb
// tone centred / level off, tremolo off, a little Tone Cut, master near the
// loudness reference, Input on Top Boost (the classic Vox sound), Bright mid.
static const float kEN30Def[kParamCount] = {
    0.00f, 0.50f, 0.60f, 0.50f,
    0.50f, 0.00f, 0.30f, 0.00f,
    0.30f, 0.72f, 1.00f, 0.50f,
};

#endif // EN30_PARAMS_H
