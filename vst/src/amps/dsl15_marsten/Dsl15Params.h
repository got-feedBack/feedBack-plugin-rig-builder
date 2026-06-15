#ifndef DSL15_PARAMS_H
#define DSL15_PARAMS_H

/*
 * MARSTEN DSL15 = Marshall DSL15H (15 W head) — the front panel, 1:1, from the
 * local schematic (DSL15-60-02 iss4, "15W MAIN BOARD"). Parody brand "Marsten"
 * (the same brand used for the GM-2 / UV-1 Marshall-copy pedals and the larger
 * Marsten DSL100); the in-app face must never read "Marshall".
 *
 * The DSL15 is the little-brother DSL: a single shared 4x ECC83 preamp feeding a
 * 2x 6V6 (~15 W) power amp via the output transformer (TXOP-91001), with two
 * footswitchable channels and a simplified panel (no Resonance pot, no reverb on
 * the head, single Master):
 *   CLASSIC GAIN : Clean -> Crunch    — Gain + Volume  (VR3 GAIN / VR2 VOLUME)
 *   ULTRA GAIN   : high-gain OD        — Gain + Volume  (VR1 GAIN / VR4 VOLUME)
 *   shared EQ    : Bass, Middle, Treble   (VR7 / VR6 B20K / VR5 B200K)
 *   DEEP switch  : low-frequency power-amp resonance boost (SW1B + R94/C68/C69)
 *   TONE SHIFT   : mid-scoop switch on the EQ board
 *   power amp    : Presence (VR8 C10K, high-frequency NFB)
 *
 * the game mapping (rs_knob_to_vst_param.json): RS Gain -> ULTRA GAIN with the
 * Channel pinned to Ultra (the DSL15 lead voice). Bass/Mid/Treble -> tone stack,
 * Pres -> Presence. The per-channel Gain/Vol + Deep/Tone Shift sit at musical
 * defaults (_static) and stay editable by hand.
 */
enum Dsl15ParamId
{
    kChannel = 0,    // CLASSIC(0) / ULTRA(1) select       [RS Gain -> Ultra pinned]
    kClassicGain,    // CLASSIC GAIN  (clean -> crunch)
    kClassicVolume,  // CLASSIC VOLUME
    kUltraGain,      // ULTRA GAIN    (high-gain OD)        [RS Gain]
    kUltraVolume,    // ULTRA VOLUME
    kPresence,       // power-amp high-frequency NFB        [RS Pres]
    kBass,           // tone stack BASS                     [RS Bass]
    kMiddle,         // tone stack MIDDLE                   [RS Mid]
    kTreble,         // tone stack TREBLE                   [RS Treble]
    kDeep,           // DEEP low-end resonance switch  Off(0)/On(1)
    kToneShift,      // TONE SHIFT mid-scoop switch     Off(0)/On(1)
    kParamCount
};

static const char* const kDsl15Names[kParamCount] = {
    "Channel", "Classic Gain", "Classic Volume",
    "Ultra Gain", "Ultra Volume", "Presence",
    "Bass", "Middle", "Treble", "Deep", "Tone Shift",
};

static const char* const kDsl15Symbols[kParamCount] = {
    "channel", "classicgain", "classicvolume",
    "ultragain", "ultravolume", "presence",
    "bass", "middle", "treble", "deep", "toneshift",
};

static const float kDsl15Min[kParamCount] = {
    0,0,0, 0,0,0, 0,0,0, 0,0 };
static const float kDsl15Max[kParamCount] = {
    1,1,1, 1,1,1, 1,1,1, 1,1 };
// Manual-insert defaults: Ultra channel (the DSL15 lead voice), moderate gain;
// Classic set to a usable edge-of-crunch; tone stack centred-ish; Deep/Tone
// Shift off.
static const float kDsl15Def[kParamCount] = {
    1.00f,            // Channel = Ultra
    0.50f, 0.50f,     // Classic Gain / Volume
    0.60f, 0.50f,     // Ultra Gain / Volume
    0.50f,            // Presence
    0.50f, 0.50f, 0.60f, // Bass, Middle, Treble
    0.00f, 0.00f,     // Deep off, Tone Shift off
};

#endif // DSL15_PARAMS_H
