#ifndef TW26_PARAMS_H
#define TW26_PARAMS_H

/*
 * BENDER DELUXE = Fender '57 Deluxe (tweed 5E3) — the FULL panel, 1:1.
 *
 * The real 5E3 has only THREE knobs: Tone, Instrument Volume, Mic Volume — plus
 * 4 input jacks (Instrument Hi/Lo, Mic Hi/Lo), a power + standby switch, fuse and
 * a pilot jewel. There is NO Gain, Bass, Mid or Presence pot: on a 5E3 the
 * VOLUME is the gain (it dirties up as you turn it), the single Tone is the only
 * EQ, and the famous trick is to JUMPER the two channels so the Mic volume fills
 * body/mids underneath the Instrument channel.
 *
 * the game mapping (rs_knob_to_vst_param.json):
 *   Gain  -> Inst Vol (the 5E3 gain = volume)
 *   Treble-> Tone     (the single tweed Tone control)
 *   Mid   -> Mic Vol  (jumper the Mic channel in for body/mids)
 *   Bright-> Bright    (Instrument bright input 1 vs normal input 2)
 *   Bass  -> Bass      (no 5E3 bass pot -> a hidden low shelf)
 *   Pres  -> Presence  (no 5E3 presence pot -> a hidden power-amp top lift)
 * Bright/Bass/Presence have no front-panel knob (the real amp has none); they are
 * driven only by the game transformation. Bright is shown as the input cable.
 */
enum TW26ParamId
{
    kTone = 0,    // single tweed Tone control            [RS Treble]
    kInstVol,     // Instrument channel Volume (= gain)   [RS Gain]
    kMicVol,      // Mic channel Volume (jumpered body)   [RS Mid]
    kBright,      // Instrument bright input (1 bright/2 normal)  [RS Bright]
    kBass,        // hidden low shelf (no 5E3 bass pot)   [RS Bass]
    kPresence,    // hidden power-amp top lift (no 5E3 pot) [RS Pres]
    kParamCount
};

static const char* const kTW26Names[kParamCount] = {
    "Tone", "Inst Vol", "Mic Vol", "Bright", "Bass", "Presence",
};

static const char* const kTW26Symbols[kParamCount] = {
    "tone", "instvol", "micvol", "bright", "bass", "presence",
};

static const float kTW26Min[kParamCount] = { 0,0,0,0,0,0 };
static const float kTW26Max[kParamCount] = { 1,1,1,1,1,1 };
// Manual-insert defaults: a usable tweed tone — Tone ~6, Instrument volume just
// into breakup, Mic volume off (no jumper), bright input, neutral Bass/Presence.
static const float kTW26Def[kParamCount] = {
    0.60f, 0.45f, 0.00f, 1.00f, 0.50f, 0.50f,
};

#endif // TW26_PARAMS_H
