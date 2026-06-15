#ifndef SUPERDRIVE_PARAMS_H
#define SUPERDRIVE_PARAMS_H

/*
 * GANDDI SUPERDRIVE 45 = Budda Superdrive 45 Series II — the FULL front panel,
 * 1:1, from the local manual + schematic (Superdrive45_manual.pdf /
 * BuddaSuperdrive80Schematic.jpg). Parody brand "Ganddi"; the oval badge + face
 * must never read "Budda".
 *
 * A two-channel, hot-rodded EL34/KT66 amp (3x 12AX7 + 2x KT66 ~45W + 5AR4):
 *   - RHYTHM channel (clean -> edge of breakup), gain set by the RHYTHM knob.
 *   - HI-GAIN channel (the "Drive"/lead voice), cascaded 12AX7 gain set by DRIVE.
 *   The MASTER pull selects the channel (in = Rhythm, out = Hi-gain); footswitch
 *   does the same live. Shared Bass/Mid/Treble tone stack + a single MASTER.
 *
 * Push-pull pots (from the manual):
 *   - MASTER  pull = CHANNEL  (Rhythm in / Hi-gain out)
 *   - MID     pull = "MODERN" (scoops mids, lifts bass+treble; hi-gain only)
 *   - RHYTHM  pull = BRITE    (treble boost on the rhythm/clean channel)
 *
 * the game mapping (rs_knob_to_vst_param.json): RS Gain -> DRIVE (the hi-gain
 * distortion), Bass/Mid/Treble -> tone stack. Channel pinned to Hi-gain + Modern
 * ON via _static (the gain_variants were captured "Modern"); Master/Rhythm sit
 * at musical defaults. All editable by hand (incl. the three pulls on the face).
 */
enum SuperdriveParamId
{
    kMaster = 0,   // MASTER volume (into the power amp)
    kBass,         // BASS  tone stack                       [RS Bass]
    kMid,          // MID   tone stack                       [RS Mid]
    kTreble,       // TREBLE tone stack                      [RS Treble]
    kDrive,        // DRIVE — hi-gain channel distortion     [RS Gain]
    kRhythm,       // RHYTHM — clean channel gain/volume
    kChannel,      // pull MASTER:  Rhythm(0) / Hi-gain(1)
    kModern,       // pull MID:     classic(0) / Modern(1)
    kBrite,        // pull RHYTHM:  off(0) / Brite(1)
    kParamCount
};

static const char* const kSuperNames[kParamCount] = {
    "Master", "Bass", "Mid", "Treble", "Drive", "Rhythm",
    "Channel", "Modern", "Brite",
};

static const char* const kSuperSymbols[kParamCount] = {
    "master", "bass", "mid", "treble", "drive", "rhythm",
    "channel", "modern", "brite",
};

static const float kSuperMin[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kSuperMax[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: Hi-gain channel, a singing crunch off DRIVE, tone
// stack centred-ish, Master ~9 o'clock, classic voicing. Pull MID for Modern,
// pull MASTER in for the Rhythm channel, pull RHYTHM for Brite — by hand.
static const float kSuperDef[kParamCount] = {
    0.42f, 0.50f, 0.50f, 0.55f, 0.48f, 0.40f,
    1.0f, 0.0f, 0.0f,
};

#endif // SUPERDRIVE_PARAMS_H
