#ifndef JCM800_PARAMS_H
#define JCM800_PARAMS_H

/*
 * MARSTEN JCM800 = Marshall JCM800 2204 (50W master-volume head) — the FULL
 * front panel, 1:1, from the official Marshall 2204 STD schematic (2204prem.gif
 * + 2204pwrm.gif, 19-5-88). Parody brand "Marsten" (same family as the Marsten
 * Plexi / DSL100). The face must never read "Marshall".
 *
 * Single-channel cascaded-gain head: V1a -> PREAMP VOL (the gain) -> V1b -> V2a
 * -> V2b cathode follower -> Marshall TMB tone stack (Treble 220k, Bass 1M,
 * Middle 22k, slope 33k, 470pF/22n/22n) -> MASTER VOL -> 2x EL34 (~50W) +
 * PRESENCE (power-amp NFB). The EMS is this same circuit + a HI/LO switch.
 *
 * the game gear Amp_MarshallJCM800. RS: Gain -> PREAMP VOL (the 2204 drive),
 * Bass/Mid/Treble -> tone stack, Pres -> Presence. Master pinned via _static.
 */
enum Jcm800ParamId
{
    kGain = 0,      // PREAMP VOL — the 2204 cascaded drive               [RS Gain]
    kBass,          // BASS    Marshall tone stack                        [RS Bass]
    kMiddle,        // MIDDLE  Marshall tone stack (22k)                  [RS Mid]
    kTreble,        // TREBLE  Marshall tone stack (220k, 470pF)          [RS Treble]
    kPresence,      // PRESENCE — power-amp NFB                           [RS Pres]
    kVolume,        // MASTER VOL — master volume
    kParamCount
};

static const char* const kJcm800Names[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Presence", "Volume",
};
static const char* const kJcm800Symbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "presence", "volume",
};
static const float kJcm800Min[kParamCount] = { 0,0,0,0,0,0 };
static const float kJcm800Max[kParamCount] = { 1,1,1,1,1,1 };
// Manual-insert defaults: the classic 80s 2204 crunch — Preamp past noon, tone
// centred, Presence + Master at musical defaults.
static const float kJcm800Def[kParamCount] = {
    0.62f, 0.50f, 0.50f, 0.55f, 0.50f, 0.60f,
};

#endif // JCM800_PARAMS_H
