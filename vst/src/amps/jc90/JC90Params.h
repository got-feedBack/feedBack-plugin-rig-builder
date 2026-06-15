#ifndef JC90_PARAMS_H
#define JC90_PARAMS_H

/*
 * RONALD JC-90 = Roland JC-90 "Jazz Chorus" — the FULL front panel, 1:1, from
 * the local schematic (JC-90.pdf, S. Nagata). Parody brand "Ronald"; the face
 * must never read "Roland".
 *
 * A SOLID-STATE amp (M5218 op-amps + transistor power amp, NO tubes): a clean,
 * high-headroom preamp with a diode-clipping DISTORTION circuit, a passive
 * tone stack (+ a HI-TREBLE brightness), a spring REVERB, and the signature
 * analogue BBD stereo CHORUS / Vibrato (the dry feeds one speaker, the
 * pitch-modulated wet the other -> the famous wide Jazz Chorus shimmer).
 *
 * Panel (1:1): DISTORTION, VOLUME, HI-TREBLE, TREBLE, MIDDLE, BASS, REVERB,
 * RATE, DEPTH + a CHORUS 3-way (Vibrato / Off / Chorus).
 *
 * the game mapping (rs_knob_to_vst_param.json): Gain -> Distortion (clean at 0
 * -> the gritty solid-state drive), Treble/Mid/Bass -> tone stack, Pres ->
 * Hi-Treble. Reverb + Chorus sit OFF for songs (the game adds those via its
 * own pedals/racks) and stay editable by hand.
 */
enum JC90ParamId
{
    kDistortion = 0, // DISTORTION (diode-clip drive)   [RS Gain]
    kVolume,         // VOLUME
    kHiTreble,       // HI-TREBLE (extra brightness)     [RS Pres]
    kTreble,         // TREBLE                            [RS Treble]
    kMiddle,         // MIDDLE                            [RS Mid]
    kBass,           // BASS                              [RS Bass]
    kReverb,         // spring REVERB level
    kRate,           // CHORUS / Vibrato RATE (speed)
    kDepth,          // CHORUS / Vibrato DEPTH
    kChorusMode,     // Chorus/Fixed(0) / Off(0.5) / Vibrato/Manual(1) — matches the real knob (FIXED left, OFF top, MANUAL right)
    kParamCount
};

static const char* const kJC90Names[kParamCount] = {
    "Distortion", "Volume", "Hi-Treble", "Treble", "Middle", "Bass",
    "Reverb", "Rate", "Depth", "Chorus",
};

static const char* const kJC90Symbols[kParamCount] = {
    "distortion", "volume", "hitreble", "treble", "middle", "bass",
    "reverb", "rate", "depth", "chorus",
};

static const float kJC90Min[kParamCount] = { 0,0,0,0,0,0,0,0,0,0 };
static const float kJC90Max[kParamCount] = { 1,1,1,1,1,1,1,1,1,1 };
// Defaults: the JC clean (Distortion off), Chorus OFF (0.5 = the 3-way midpoint)
// so the game songs aren't chorused by default — RS adds chorus via its own
// pedal; turn the Chorus knob to FIXED(0)/MANUAL(1) by hand for the iconic JC.
static const float kJC90Def[kParamCount] = {
    0.00f, 0.60f, 0.50f, 0.60f, 0.50f, 0.50f,
    0.20f, 0.40f, 0.55f, 0.50f,
};

#endif // JC90_PARAMS_H
