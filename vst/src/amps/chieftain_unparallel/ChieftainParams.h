#ifndef CHIEFTAIN_PARAMS_H
#define CHIEFTAIN_PARAMS_H

/*
 * UNPARALLEL CHIEFTAIN = Matchless Chieftain (Reverb), Mark Sampson, hand-traced
 * 7-page schematic. Parody brand "RigBuilder"; the face must never read
 * "Matchless" or "Chieftain" (parody "Unparallel Chieftain"). the game gear:
 * Amp_BT15.
 *
 * A single-channel CLEAN/crunch boutique head: 2x EL34 CATHODE-BIASED class AB
 * (~40W, moderate sag, big headroom). Lots of clean before breakup -> a
 * Fender-meets-Marshall voice. Stays cleaner / has more headroom than the
 * Mark/Boogie -- so do NOT over-saturate.
 *
 * Signal flow (per the schematic):
 *   V1 12AX7 (68k/1M in, 1k5 cathode) -> a Marshall-style TMB tone stack:
 *     BASS 1MRA, MID 250kA, TREBLE 1MA with a LARGE 5100pF (5.1nF) treble cap
 *     (=> warmer, lower-acting treble than a Marshall's 500pF), slope/coupling
 *     .0022 / 560p / 100k / 220k / 150k; V2 12AX7 -> VOLUME 250kA.
 *   Phase inverter (12AX7, long-tail): MASTER 500kA + BRILLIANCE 500kA (a
 *     presence/high-shelf on the PI via a .0047 cap -> higher Brilliance = more
 *     top sparkle).
 *   Spring REVERB (12AX7 driver + tank + 12AX7 recovery + 100kA Reverb level).
 *   Power: 2x EL34, 270ohm/250uF cathode bias, OT WTI9356, GZ34 rect, 4/8/16 ohm.
 *
 * the game: RS exposes Gain/Bass/Mid/Treble. RS Gain -> VOLUME (drives the
 * preamp into the EL34s). Bass/Mid/Treble -> the TMB tone stack. Brilliance /
 * Master / Reverb set by hand on the face.
 */
enum ChieftainParamId
{
    kVolume = 0,    // VOLUME 250kA  — preamp drive into the power stage  [RS Gain]
    kBass,          // BASS 1MRA     tone stack                          [RS Bass]
    kMiddle,        // MIDDLE 250kA  tone stack                          [RS Mid]
    kTreble,        // TREBLE 1MA    tone stack (5.1nF cap, warm)        [RS Treble]
    kBrilliance,    // BRILLIANCE 500kA — presence high-shelf on the PI
    kMaster,        // MASTER 500kA  — power-amp master
    kReverb,        // REVERB 100kA  — spring reverb mix
    kParamCount
};

static const char* const kChieftainNames[kParamCount] = {
    "Volume", "Bass", "Middle", "Treble", "Brilliance", "Master", "Reverb",
};

static const char* const kChieftainSymbols[kParamCount] = {
    "volume", "bass", "middle", "treble", "brilliance", "master", "reverb",
};

static const float kChieftainMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kChieftainMax[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: a clean-ish boutique voice — Volume just past noon,
// flat-ish tone stack, Brilliance backed off, Master open, reverb off.
static const float kChieftainDef[kParamCount] = {
    0.55f, 0.50f, 0.50f, 0.55f, 0.40f, 0.70f, 0.00f,
};

#endif // CHIEFTAIN_PARAMS_H
