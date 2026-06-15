#ifndef ENGEL_FIREBALL_PARAMS_H
#define ENGEL_FIREBALL_PARAMS_H

/*
 * ENGEL FIREBALL = ENGL Fireball 100 (EN-50) — the front panel, 1:1, from the
 * local schematic (engl-fireball-amplifier-schematic_new.pdf, "ENGL Gerätebau
 * GmbH 625"). Parody brand "Engel" (ENGL -> Engel, the German word the company
 * name puns on); the in-app face must NEVER read "ENGL".
 *
 * A 2-channel high-gain modern metal head (~100W). Two footswitchable voices off
 * a shared ECC83 preamp + tube power amp:
 *   CLEAN  : low-gain, headroomy rhythm voice (Clean Gain)
 *   LEAD   : the Ultra high-gain cascade — tight, aggressive, scooped-capable,
 *            MORE gain than a JCM800 (Lead Gain -> Lead Volume)
 *   shared : a Marshall-ish passive TMB tone stack (Bass / Middle / Treble)
 *   voicing: Bright (treble boost), Bottom (low-end boost), Mid Boost (mid push)
 *   power  : Presence (high-frequency NFB) + dual Master
 *
 * the game gear: Amp_EN50. RS Gain -> LEAD GAIN (Channel pinned to LEAD via
 * _static); Bass/Mid/Treble -> tone stack; Pres -> Presence. The clean gain,
 * masters and voicing switches sit at musical defaults and stay editable.
 */
enum EngelFireballParamId
{
    kCleanGain = 0,  // CLEAN GAIN   — clean channel input gain
    kLeadGain,       // LEAD GAIN    — Ultra high-gain cascade drive  [RS Gain]
    kBass,           // BASS         tone stack                       [RS Bass]
    kMiddle,         // MIDDLE       tone stack                       [RS Mid]
    kTreble,         // TREBLE       tone stack                       [RS Treble]
    kLeadVolume,     // LEAD VOLUME  — lead channel volume
    kMaster,         // MASTER       — global output master
    kPresence,       // PRESENCE     — power-amp high-frequency NFB    [RS Pres]
    kChannel,        // channel: Clean(0) / Lead(1)
    kBright,         // BRIGHT     treble-boost voicing switch  Off(0)/On(1)
    kBottom,         // BOTTOM     low-end-boost voicing switch  Off(0)/On(1)
    kMidBoost,       // MID BOOST  mid-push voicing switch       Off(0)/On(1)
    kParamCount
};

static const char* const kEngelFireballNames[kParamCount] = {
    "Clean Gain", "Lead Gain", "Bass", "Middle", "Treble", "Lead Volume",
    "Master", "Presence", "Channel", "Bright", "Bottom", "Mid Boost",
};

static const char* const kEngelFireballSymbols[kParamCount] = {
    "cleangain", "leadgain", "bass", "middle", "treble", "leadvolume",
    "master", "presence", "channel", "bright", "bottom", "midboost",
};

static const float kEngelFireballMin[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0,0 };
static const float kEngelFireballMax[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: LEAD channel (the Fireball lead voice), the high-gain
// cascade off Lead Gain, a usable clean set behind it, the tone stack centred-ish
// (Middle slightly scooped), masters at musical defaults, Presence centred,
// voicing switches OFF. Switch to Clean / engage Bright / Bottom / Mid Boost by
// hand on the face.
static const float kEngelFireballDef[kParamCount] = {
    0.50f, 0.65f, 0.50f, 0.45f, 0.60f, 0.50f,
    0.60f, 0.50f, 1.00f, 0.00f, 0.00f, 0.00f,
};

#endif // ENGEL_FIREBALL_PARAMS_H
