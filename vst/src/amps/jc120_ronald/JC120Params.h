#ifndef JC120_PARAMS_H
#define JC120_PARAMS_H

/*
 * RONALD JC-120 = Roland JC-120 "Jazz Chorus" — the Effect channel (CH-2) front
 * panel, 1:1, from the local service manual (JC-120/JC-160 Service Notes, 5th
 * Ed.). Parody brand "Ronald" (Roland -> Ronald); the in-app face must never
 * read "Roland".
 *
 * A SOLID-STATE 120W amp (NJM4558 / M5218-class op-amps + 2SD736A transistor
 * power amp, NO tubes), 2x 30cm (12") speakers. The Effect channel has a clean,
 * high-headroom preamp with a diode-clipping DISTORTION circuit (10kC pot w/SW),
 * a passive tone stack (BASS 250kA / MIDDLE 10kB / TREBLE 250kB, with a BRIGHT
 * switch), a spring REVERB (Z-3F unit, 10kB level), and the signature analogue
 * BBD stereo CHORUS / Vibrato (MN3007 BBD + MN3101/MN3004 clock) — the dry feeds
 * one speaker, the pitch-modulated wet the other -> the famous wide Jazz Chorus
 * shimmer.
 *
 * Tone control range (per the spec, VOLUME at center): TREBLE 17dB @10kHz,
 * MIDDLE 13dB @350Hz, BASS 14dB @50Hz, BRIGHT 5dB @10kHz.
 *
 * Panel (1:1, the Effect channel): VOLUME, TREBLE, MIDDLE, BASS, DISTORTION,
 * REVERB + a VIB/CHORUS section (SPEED, DEPTH + a 3-way Off/Chorus/Vibrato
 * switch — the real ESL-3654 lever).
 *
 * the game mapping (rs_knob_to_vst_param.json): Gain -> Distortion (clean at 0
 * -> the gritty solid-state drive), Treble/Mid/Bass -> tone stack. Reverb +
 * Chorus sit OFF for songs (the game adds those via its own pedals/racks) and
 * stay editable by hand.
 */
enum JC120ParamId
{
    kVolume = 0,     // VOLUME
    kTreble,         // TREBLE                            [RS Treble]
    kMiddle,         // MIDDLE                            [RS Mid]
    kBass,           // BASS                              [RS Bass]
    kDistortion,     // DISTORTION (diode-clip drive)     [RS Gain]
    kReverb,         // spring REVERB level
    kSpeed,          // CHORUS / Vibrato SPEED (rate)
    kDepth,          // CHORUS / Vibrato DEPTH
    kChorus,         // Off(0) / Chorus(0.5) / Vibrato(1) — the 3-way VIB/CHORUS lever
    kParamCount
};

static const char* const kJC120Names[kParamCount] = {
    "Volume", "Treble", "Middle", "Bass", "Distortion",
    "Reverb", "Speed", "Depth", "Chorus",
};

static const char* const kJC120Symbols[kParamCount] = {
    "volume", "treble", "middle", "bass", "distortion",
    "reverb", "speed", "depth", "chorus",
};

static const float kJC120Min[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kJC120Max[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Defaults: the JC clean (Distortion off), Chorus OFF (0 = the 3-way left
// position) so the game songs aren't chorused by default — turn the Chorus
// switch to Chorus(0.5)/Vibrato(1) by hand for the iconic JC shimmer.
static const float kJC120Def[kParamCount] = {
    0.50f, 0.55f, 0.50f, 0.50f, 0.00f,
    0.00f, 0.40f, 0.50f, 0.00f,
};

#endif // JC120_PARAMS_H
