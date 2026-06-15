#ifndef JIMMYBEAN_PARAMS_H
#define JIMMYBEAN_PARAMS_H

/*
 * CITRUS JIMMY BEAN = Orange Jimmy Bean JB150 (1975-76) — the front panel,
 * RECONSTRUCTED (no schematic exists). Parody brand "Citrus" (Orange -> Citrus);
 * the in-app face must never read "Orange".
 *
 * A ~150W SOLID-STATE (transistor, op-amp) TWIN-CHANNEL head with a built-in
 * TREMOLO and a switchable SUSTAIN circuit (denim/leather styling). It is mostly
 * a clean/loud solid-state amp; the SUSTAIN circuit adds gain/dirt (an op-amp
 * compressor/soft-clip); the TREMOLO is an amplitude LFO. SOLID-STATE: NO tube
 * sag / asymTube cascades / power-tube models.
 *
 * Panel (8 controls): VOLUME, BASS, TREBLE, SUSTAIN, SPEED, DEPTH, CHANNEL,
 * BRIGHT. Baxandall-ish BASS/TREBLE (no MID). BRIGHT = a high-shelf switch.
 * CHANNEL 0/1 = the two channels (Ch2 a touch brighter / more gain).
 *
 * the game mapping: RS Gain -> SUSTAIN (the dirt/sustain), RS Bass -> Bass,
 * RS Treble -> Treble (no Mid on this amp).
 */
enum JimmyBeanParamId
{
    kVolume = 0, // VOLUME  (master output level)
    kBass,       // BASS    (Baxandall bass shelf)            [RS Bass]
    kTreble,     // TREBLE  (Baxandall treble shelf)          [RS Treble]
    kSustain,    // SUSTAIN (solid-state comp/soft-clip dirt) [RS Gain]
    kSpeed,      // SPEED   (tremolo rate, 2..8 Hz)
    kDepth,      // DEPTH   (tremolo amount; 0 = OFF)
    kChannel,    // CHANNEL (0/1 = the two channels)
    kBright,     // BRIGHT  (treble high-shelf switch)
    kParamCount
};

static const char* const kJimmyBeanNames[kParamCount] = {
    "Volume", "Bass", "Treble", "Sustain", "Speed", "Depth", "Channel", "Bright",
};

static const char* const kJimmyBeanSymbols[kParamCount] = {
    "volume", "bass", "treble", "sustain", "speed", "depth", "channel", "bright",
};

static const float kJimmyBeanMin[kParamCount] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const float kJimmyBeanMax[kParamCount] = { 1, 1, 1, 1, 1, 1, 1, 1 };
// Defaults: a clean/loud solid-state voice. Volume 0.55, tone flat-ish, Sustain
// off (clean), tremolo set but Depth 0 (OFF), Channel 1 (Ch1), Bright off.
static const float kJimmyBeanDef[kParamCount] = {
    0.55f, 0.50f, 0.55f, 0.00f, 0.40f, 0.00f, 0.00f, 0.00f,
};

#endif // JIMMYBEAN_PARAMS_H
