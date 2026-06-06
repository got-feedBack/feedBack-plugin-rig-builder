#ifndef MAZ38_PARAMS_H
#define MAZ38_PARAMS_H

/*
 * MR. Y MAZ 38 = Dr. Z Maz 38 (Senior NR) for Rocksmith's Amp_GB38. Parody brand
 * "Mr. Y"; the in-app face must never read "Dr. Z" or "Maz".
 *
 * The Maz 38 shares the Maz line's preamp + tone stack (modelled from the Maz 18
 * Jr schematic, identical front-end), but a BIGGER power amp: 4x EL84 (~38W) with
 * SOLID-STATE rectification -> much more headroom, tighter/firmer lows, breaks up
 * later, far less sag than the 2x EL84 / GZ34 Maz 18. The Senior NR panel has NO
 * reverb. Voiced Vox-meets-Fender: chimey, midrange-forward, but with American
 * clean punch.
 *
 * Panel (per the Senior NR photo): VOLUME, TREBLE, MIDDLE, BASS, CUT, MASTER.
 * RS: Gain -> VOLUME (the preamp/drive), Bass/Mid/Treble -> tone stack; Cut +
 * Master pinned via _static.
 */
enum Maz38ParamId
{
    kVolume = 0,    // VOLUME — preamp drive                            [RS Gain]
    kTreble,        // TREBLE  tone stack                               [RS Treble]
    kMiddle,        // MIDDLE  tone stack                               [RS Mid]
    kBass,          // BASS    tone stack                               [RS Bass]
    kCut,           // CUT — post treble cut (higher = darker)
    kMaster,        // MASTER volume
    kParamCount
};

static const char* const kMaz38Names[kParamCount] = {
    "Volume", "Treble", "Middle", "Bass", "Cut", "Master",
};
static const char* const kMaz38Symbols[kParamCount] = {
    "volume", "treble", "middle", "bass", "cut", "master",
};
static const float kMaz38Min[kParamCount] = { 0,0,0,0,0,0 };
static const float kMaz38Max[kParamCount] = { 1,1,1,1,1,1 };
// Manual-insert defaults: a chimey clean/edge-of-breakup — Volume past noon, tone
// centred, Cut just below noon, Master up (the 38 has headroom to spare).
static const float kMaz38Def[kParamCount] = {
    0.60f, 0.55f, 0.50f, 0.50f, 0.40f, 0.70f,
};

#endif // MAZ38_PARAMS_H
