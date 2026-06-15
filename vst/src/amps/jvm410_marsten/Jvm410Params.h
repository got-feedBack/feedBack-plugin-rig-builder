#ifndef JVM410_PARAMS_H
#define JVM410_PARAMS_H

/*
 * MARSTEN JVM410 = Marshall JVM410H — the game gear Amp_MarshallJVM410H. Parody
 * brand "Marsten" (Marshall -> Marsten; same family as the Marsten DSL100 /
 * JCM800 / Bluesbreaker). The in-app face must never read "Marshall".
 *
 * Local reference (modelled from the local schematic, component-by-component):
 *   amps/Marshall JVM410/Marshall_jvm410_sch.pdf
 *     SHT1 PRE AMP (JVM410-60-02) — input, FX loop, op-amp reverb, NFB
 *     SHT2 POWER AMP / DI OUT     — 4x EL34 (V1..V4), phase-inverter, DI
 *     FRONT PANEL 1 (JVM410-61-02) — the four channel strips + tone stacks:
 *       CLEAN  : VR219 Treble / VR217 Bass / VR218 Mid / VR206 Clean Volume
 *                + shared clean gain block VR209/207/208 (CLEAN GAIN)
 *       CRUNCH : VR204 Treble / VR202 Bass / VR203 Mid / VR216 Crunch Volume
 *                + VR220 CRUNCH GAIN
 *       OD1    : VR201 Treble / VR202 Bass / VR203 Mid / ...   + VR205 OD1 GAIN
 *       OD2    : VR214 Treble / VR212 Bass / VR213 Mid / VR211 DD2 Volume
 *                + VR215 OD2 GAIN
 *     FRONT PANEL 2 — RESONANCE (VR305), PRESENCE (VR326), MASTER 1/2, the
 *       per-channel green/orange/red mode relays (LATCH_1/LATCH_2 logic).
 *   amps/Marshall JVM410/2.jpg — the front panel (for the canvas).
 *
 * The JVM410H is a 4-CHANNEL 100W EL34 head: CLEAN / CRUNCH / OD1 / OD2, each
 * with a 3-mode (green / orange / red) voicing that cascades extra preamp gain &
 * saturation. The real amp lets you store all four channels; the game plays one
 * tone at a time, so this models the SELECTED channel + mode (a single sound).
 * That is the only simplification — every channel/mode is reachable via kChannel
 * (Clean 0..0.25 / Crunch .25..0.5 / OD1 .5..0.75 / OD2 .75..1) + kMode
 * (green 0 / orange 0.5 / red 1). One shared Marshall TMB tone stack voices all
 * four (the real channels share the same R/C topology, just different pot tapers)
 * + Presence (HF NFB) + Resonance (LF NFB) + Master + op-amp Reverb.
 *
 * the game: RS Gain -> GAIN (kChannel pinned to OD1 + kMode orange via the song
 * mapping); RS Bass/Mid/Treble -> the tone stack; RS Pres -> PRESENCE. The other
 * controls (Channel/Mode/Volume/Resonance/Master/Reverb) sit at musical defaults
 * via _static and stay editable by hand.
 */
enum Jvm410ParamId
{
    kChannel = 0,   // CLEAN(0..0.25) / CRUNCH(.25..0.5) / OD1(.5..0.75) / OD2(.75..1)
    kMode,          // voicing: green(0) / orange(0.5) / red(1) — adds preamp gain+sat
    kGain,          // GAIN — preamp drive of the selected channel   [RS Gain]
    kVolume,        // VOLUME — selected channel volume (into the master)
    kBass,          // BASS   — Marshall TMB tone stack               [RS Bass]
    kMiddle,        // MIDDLE — Marshall TMB tone stack               [RS Mid]
    kTreble,        // TREBLE — Marshall TMB tone stack               [RS Treble]
    kPresence,      // PRESENCE — power-amp HF negative feedback       [RS Pres]
    kResonance,     // RESONANCE — power-amp LF negative feedback (low-end thump)
    kMaster,        // MASTER — power-amp master volume
    kReverb,        // REVERB — op-amp digital reverb mix (off at 0)
    kParamCount
};

static const char* const kJvm410Names[kParamCount] = {
    "Channel", "Mode", "Gain", "Volume", "Bass", "Middle", "Treble",
    "Presence", "Resonance", "Master", "Reverb",
};

static const char* const kJvm410Symbols[kParamCount] = {
    "channel", "mode", "gain", "volume", "bass", "middle", "treble",
    "presence", "resonance", "master", "reverb",
};

static const float kJvm410Min[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0 };
static const float kJvm410Max[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1 };

// Manual-insert defaults: Channel 0.66 (OD1), Mode 0.5 (orange), a singing OD1
// rhythm/lead at musical settings, reverb off. RS pins Channel/Mode and sweeps
// GAIN; the rest stay editable on the face.
static const float kJvm410Def[kParamCount] = {
    0.66f, 0.50f, 0.60f, 0.50f, 0.50f, 0.50f, 0.60f,
    0.50f, 0.50f, 0.60f, 0.00f,
};

#endif // JVM410_PARAMS_H
