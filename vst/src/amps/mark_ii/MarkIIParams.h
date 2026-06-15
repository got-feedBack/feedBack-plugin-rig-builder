#ifndef MARK_II_PARAMS_H
#define MARK_II_PARAMS_H

/*
 * SILLA BOOGIE MARK II = Mesa/Boogie Mark IIB — the FULL front panel, 1:1, from
 * the local schematic (boogie_mkii.pdf, "MESA/Boogie Mark II Rev.B"). Parody
 * brand "Silla" (Mesa = table -> Silla = chair; same family as the Silla Boogie
 * Mark III / Duo Rectifier). The face must never read "Mesa" or "Boogie".
 *
 * the game gear: Amp_CA38 (the gear-map name said "Mark IV", but the CA_38
 * folder + schematic + photos are the Mark IIB). RS exposes only Gain/Bass/Mid/
 * Treble, so RS Gain -> LEAD DRIVE (the cascaded lead distortion), Channel pinned
 * to LEAD via _static; Bass/Mid/Treble -> the (scooped) Fender-derived tone stack.
 *
 * Panel (1:1, left->right, per the photos): INPUT, FOOTSWITCH, then the knob row:
 *   VOLUME 1 (pull BRIGHT), TREBLE (pull SHIFT), BASS, MIDDLE, MASTER 1 (pull
 *   GAIN BOOST), LEAD DRIVE (pull LEAD), LEAD MASTER (pull BRIGHT), REVERB, then
 *   the 1/2-PWR (100/60 RMS) + STANDBY + POWER toggles. NO graphic EQ on this
 *   panel (unlike the Mark III/IV). 4x 6L6GC power amp (~100W / 60W half).
 *
 * Two voices off ONE scooped Fender-derived tone stack placed BEFORE the gain
 * (the Mark signature): RHYTHM (Volume 1 -> Master 1) and LEAD (Lead Drive
 * cascade -> Lead Master), picked by the LEAD relay. Pull-SHIFT fattens the
 * treble/mid voicing; pull-GAIN-BOOST adds lead preamp gain.
 */
enum MarkIIParamId
{
    kVolume1 = 0,   // VOLUME 1 — rhythm input gain (pull BRIGHT)
    kTreble,        // TREBLE   tone stack (pull SHIFT)        [RS Treble]
    kBass,          // BASS     tone stack                     [RS Bass]
    kMiddle,        // MIDDLE   tone stack (10K, scooped)      [RS Mid]
    kMaster1,       // MASTER 1 — rhythm channel master (pull GAIN BOOST)
    kLeadDrive,     // LEAD DRIVE — cascaded lead distortion   [RS Gain]
    kLeadMaster,    // LEAD MASTER — lead channel master (pull BRIGHT)
    kReverb,        // REVERB  — spring reverb mix
    kChannel,       // channel: Rhythm(0) / Lead(1)  (the LEAD pull)
    kBright1,       // VOLUME 1 pull-BRIGHT
    kShift,         // TREBLE pull-SHIFT (fatter treble/mid voicing)
    kGainBoost,     // MASTER 1 pull-GAIN BOOST (extra lead preamp gain)
    kBrightLead,    // LEAD MASTER pull-BRIGHT
    kHalfPower,     // output power: 100 RMS(0) / 60 RMS(1)
    kParamCount
};

static const char* const kMarkIINames[kParamCount] = {
    "Volume 1", "Treble", "Bass", "Middle", "Master 1", "Lead Drive", "Lead Master",
    "Reverb", "Lead", "Bright 1", "Shift", "Gain Boost", "Bright Lead", "Half Power",
};

static const char* const kMarkIISymbols[kParamCount] = {
    "volume1", "treble", "bass", "middle", "master1", "leaddrive", "leadmaster",
    "reverb", "lead", "bright1", "shift", "gainboost", "brightlead", "halfpower",
};

static const float kMarkIIMin[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
static const float kMarkIIMax[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: LEAD channel, a singing lead off Lead Drive, the
// classic Boogie scooped tone stack (Middle low), masters at musical defaults,
// reverb off, pulls out, FULL (100W) power. Switch to Rhythm / pull Shift /
// Gain Boost / Bright on the face by hand.
static const float kMarkIIDef[kParamCount] = {
    0.50f, 0.60f, 0.50f, 0.40f, 0.50f, 0.60f, 0.50f,
    0.00f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

#endif // MARK_II_PARAMS_H
