#ifndef EMS_PARAMS_H
#define EMS_PARAMS_H

/*
 * MR. Y EMS = Dr. Z EMS — a Marshall JCM800/JTM50-style master-volume head.
 * Parody brand "Mr. Y" (Dr. Z -> Mr. Y; same family as the Mr. Y MAZ 18). The
 * in-app face must never read "Dr. Z".
 *
 * Per the build reference, the EMS is a JCM800 2204 circuit with a HI/LO gain
 * switch that drops the gain to JTM50 levels (adds a 33K input divider + lifts
 * the 0.68uF cathode-bypass on the 2nd gain stage). Panel (per the photo, l->r):
 *   PRESENCE, BASS, MIDDLE, TREBLE, VOLUME (master), GAIN (preamp) + HI/LO.
 * Cascaded 12AX7 preamp -> Marshall TMB tone stack -> master volume -> 2x EL34
 * (~50W). the game gear Amp_GB50.
 *
 * RS: Gain -> GAIN (the JCM800 preamp drive), Bass/Mid/Treble -> tone stack,
 * Pres -> Presence. Master (Volume) + HI/LO pinned via _static.
 */
enum EmsParamId
{
    kGain = 0,      // GAIN — JCM800 preamp drive (cascaded)          [RS Gain]
    kBass,          // BASS    Marshall tone stack                    [RS Bass]
    kMiddle,        // MIDDLE  Marshall tone stack                    [RS Mid]
    kTreble,        // TREBLE  Marshall tone stack                    [RS Treble]
    kPresence,      // PRESENCE — power-amp NFB                       [RS Pres]
    kVolume,        // VOLUME — master volume
    kHiLo,          // gain mode: HI(0) full JCM800 / LO(1) JTM50-ish
    kParamCount
};

static const char* const kEmsNames[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Presence", "Volume", "Hi Lo",
};
static const char* const kEmsSymbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "presence", "volume", "hilo",
};
static const float kEmsMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kEmsMax[kParamCount] = { 1,1,1,1,1,1,1 };
// Manual-insert defaults: a JCM800 crunch — Gain past noon, tone centred,
// Presence + Master at musical defaults, HI gain mode.
static const float kEmsDef[kParamCount] = {
    0.60f, 0.50f, 0.50f, 0.55f, 0.50f, 0.60f, 0.0f,
};

#endif // EMS_PARAMS_H
