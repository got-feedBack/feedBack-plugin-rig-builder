#ifndef MARK_III_PARAMS_H
#define MARK_III_PARAMS_H

/*
 * SILLA BOOGIE MARK III = Mesa/Boogie Mark III — the FULL front panel, 1:1, from
 * the local schematic (boogie_mkiii.pdf, "Tri-Mode Programmable Preamplifier").
 * Parody brand "Silla" (Mesa = table -> Silla = chair; same family as the Silla
 * Boogie Duo Rectifier). The face must never read "Mesa" or "Boogie".
 *
 * the game gear: Amp_CA85 ("Mesa Boogie Mark III Crunch"). RS exposes only
 * Gain/Bass/Mid/Treble, so RS Gain -> LEAD DRIVE (the cascaded lead distortion),
 * Channel pinned to LEAD via _static; Bass/Mid/Treble -> the (scooped) tone stack.
 *
 * Panel (1:1, left->right): INPUT, FOOTSWITCH, then 7 knobs:
 *   VOLUME (pull BRIGHT), TREBLE, BASS, MIDDLE, MASTER, LEAD DRIVE, LEAD MASTER,
 * the signature 5-band GRAPHIC EQ (80 / 240 / 750 / 2200 / 6600 Hz) with an EQ
 * IN switch, then STANDBY / POWER. A 6L6/EL34 Simul-Class power amp (~75W).
 *
 * Tone stack = the Mark Fender-derived TMB with the scooped 10K mid pot (the
 * famous Boogie mid scoop). Two voices: RHYTHM (Volume->Master) and LEAD
 * (Lead Drive cascade -> Lead Master), picked by the LEAD switch.
 */
enum MarkIIIParamId
{
    kVolume = 0,    // VOLUME — rhythm input gain (pull BRIGHT)
    kTreble,        // TREBLE  tone stack                     [RS Treble]
    kBass,          // BASS    tone stack                     [RS Bass]
    kMiddle,        // MIDDLE  tone stack (10K, scooped)      [RS Mid]
    kMaster,        // MASTER  — rhythm channel master volume
    kLeadDrive,     // LEAD DRIVE — cascaded lead distortion  [RS Gain]
    kLeadMaster,    // LEAD MASTER — lead channel master
    kEq80,          // graphic EQ  80 Hz   (0.5 = flat)
    kEq240,         // graphic EQ 240 Hz
    kEq750,         // graphic EQ 750 Hz
    kEq2200,        // graphic EQ 2200 Hz
    kEq6600,        // graphic EQ 6600 Hz
    kLead,          // channel: Rhythm(0) / Lead(1)
    kBright,        // VOLUME pull-BRIGHT
    kEqIn,          // graphic EQ in(1) / out(0)
    kParamCount
};

static const char* const kMarkIIINames[kParamCount] = {
    "Volume", "Treble", "Bass", "Middle", "Master", "Lead Drive", "Lead Master",
    "EQ 80", "EQ 240", "EQ 750", "EQ 2200", "EQ 6600",
    "Lead", "Bright", "EQ In",
};

static const char* const kMarkIIISymbols[kParamCount] = {
    "volume", "treble", "bass", "middle", "master", "leaddrive", "leadmaster",
    "eq80", "eq240", "eq750", "eq2200", "eq6600",
    "lead", "bright", "eqin",
};

static const float kMarkIIIMin[kParamCount] = { 0,0,0,0,0,0,0, 0,0,0,0,0, 0,0,0 };
static const float kMarkIIIMax[kParamCount] = { 1,1,1,1,1,1,1, 1,1,1,1,1, 1,1,1 };
// Manual-insert defaults: LEAD channel, a singing lead off Lead Drive, the
// classic Boogie scooped tone stack + a mild GEQ "V" smile (lows/highs up, 750
// down), EQ engaged. Pull VOLUME for Bright / switch to Rhythm by hand.
static const float kMarkIIIDef[kParamCount] = {
    0.50f, 0.60f, 0.45f, 0.40f, 0.45f, 0.55f, 0.50f,
    0.60f, 0.50f, 0.38f, 0.50f, 0.58f,
    1.0f, 0.0f, 1.0f,
};

#endif // MARK_III_PARAMS_H
