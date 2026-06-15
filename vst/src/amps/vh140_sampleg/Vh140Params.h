#ifndef VH140_PARAMS_H
#define VH140_PARAMS_H

/*
 * SAMPLEG VH-140C = Ampeg VH-140C — the FULL front panel, 1:1, from the local
 * service schematic (Ampeg_VH-140C.pdf, SLM Electronics). Parody brand "Sampleg"
 * (same as the Sampleg SBT-CL / V-4B). The face must never read "Ampeg".
 *
 * A SOLID-STATE hybrid 2x70W stereo guitar head (TL074 / JRC4558 op-amps + 1N914
 * diode clipping, TDA power) — NO tubes. Two channels:
 *   - CHANNEL A (clean): Gain, Low, Ultra Mid, High, Level.
 *   - CHANNEL B (lead): Gain, Low, Mid, High, Level — the famous tight,
 *     aggressive diode-clipped "VH-140C" rhythm/metal distortion.
 *   Per-channel spring REVERB (Reverb A / Reverb B) and a BBD CHORUS (Rate +
 *   per-channel Depth A / Depth B). A footswitch picks the channel.
 *
 * the game (Amp_AT120): the lead channel (B) is the iconic voice, so RS Gain ->
 * CHANNEL B Gain, Bass/Mid/Treble -> Channel B Low/Mid/High. Channel pinned to B
 * + reverb/chorus OFF for songs (RS adds those) via _static; all editable by hand.
 */
enum Vh140ParamId
{
    kChannel = 0,    // A(0) / B(1) — footswitchable
    kBGain,          // CHANNEL B Gain (the diode distortion drive)   [RS Gain]
    kBLow,           // CHANNEL B Low                                  [RS Bass]
    kBMid,           // CHANNEL B Mid                                  [RS Mid]
    kBHigh,          // CHANNEL B High                                 [RS Treble]
    kBLevel,         // CHANNEL B Level (channel volume)
    kAGain,          // CHANNEL A Gain (clean)
    kALow,           // CHANNEL A Low
    kAUltraMid,      // CHANNEL A Ultra Mid
    kAHigh,          // CHANNEL A High
    kALevel,         // CHANNEL A Level
    kReverbB,        // spring reverb send — Channel B
    kReverbA,        // spring reverb send — Channel A
    kRate,           // CHORUS rate (shared)
    kDepthB,         // CHORUS depth — Channel B
    kDepthA,         // CHORUS depth — Channel A
    kParamCount
};

static const char* const kVh140Names[kParamCount] = {
    "Channel", "B Gain", "B Low", "B Mid", "B High", "B Level",
    "A Gain", "A Low", "A Ultra Mid", "A High", "A Level",
    "Reverb B", "Reverb A", "Chorus Rate", "Depth B", "Depth A",
};

static const char* const kVh140Symbols[kParamCount] = {
    "channel", "bgain", "blow", "bmid", "bhigh", "blevel",
    "again", "alow", "aultramid", "ahigh", "alevel",
    "reverbb", "reverba", "chorusrate", "depthb", "deptha",
};

static const float kVh140Min[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
static const float kVh140Max[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: CHANNEL B (the lead voice), a tight high-gain crunch,
// EQ centred-ish, Level past noon. Reverb + Chorus OFF (turn up by hand). Channel
// A sits at a clean default for when you switch to it.
static const float kVh140Def[kParamCount] = {
    1.0f, 0.60f, 0.50f, 0.45f, 0.60f, 0.55f,
    0.40f, 0.50f, 0.50f, 0.55f, 0.50f,
    0.00f, 0.00f, 0.40f, 0.00f, 0.00f,
};

#endif // VH140_PARAMS_H
