#ifndef VH4_PARAMS_H
#define VH4_PARAMS_H

/*
 * DEEZEL VH4 = Diezel VH4 (100W, 4x EL34) — currently the CHANNEL 3 "MEGA"
 * voice, the amp's iconic tight high-gain rhythm/lead tone. Parody brand
 * "Deezel" (the in-app face never reads "Diezel"). References:
 *   - amps/Diezel VH4/dz4_preamp_documentation.pdf (Aion DZ4 — an EXACT
 *     component-level recreation of the VH4's Channel 3 audio path), and
 *   - amps/Diezel VH4/service-manual_V1-1.pdf (official, EL34 power / bias).
 *
 * The VH4's signature is a VERY tight, articulate, saturated high gain (note
 * definition even wide open) — German metal/hard-rock (Tool, Metallica,
 * Rammstein). Ch3 = Marshall-style TMB tone stack, an active DEEP (~115 Hz low
 * boost) and a real-amp PRESENCE (power-amp NFB ~4 kHz), Gain + Master.
 *
 * The first nine indices are retained for preset/automation compatibility.
 * Gain/Bass/Middle/Treble at indices 0..3 are the legacy CH3 bank; the new
 * per-channel banks are appended after kChannel so old parameter IDs never
 * move. Deep, Presence and Master remain global, like the physical master
 * section. Each channel owns Gain/Bass/Middle/Treble/Volume.
 *
 * EXTRA gear (not mapped to any RS song).
 */
enum Vh4ParamId
{
    kGain = 0,      // GAIN — drives the Ch3 12AX7 cascade (tight high-gain)
    kBass,          // BASS   Marshall tone stack (1M)
    kMiddle,        // MIDDLE Marshall tone stack (25k)
    kTreble,        // TREBLE Marshall tone stack (250k / 560pF)
    kDeep,          // DEEP — active low boost/cut (~115 Hz); noon = flat
    kPresence,      // PRESENCE — power-amp NFB high-shelf (~4 kHz); noon = flat
    kMaster,        // MASTER — output / power-amp drive
    kCabSim,        // fallback 4x12 voice: 0 = amp-only, 1 = internal cab sim
    kChannel,       // 0..1 -> Ch1 Clean / Ch2 Crunch / Ch3 Mega / Ch4 Lead

    kCh1Gain,
    kCh1Bass,
    kCh1Middle,
    kCh1Treble,
    kCh1Volume,

    kCh2Gain,
    kCh2Bass,
    kCh2Middle,
    kCh2Treble,
    kCh2Volume,

    kCh3Volume,     // CH3 tone bank reuses legacy indices 0..3

    kCh4Gain,
    kCh4Bass,
    kCh4Middle,
    kCh4Treble,
    kCh4Volume,
    kParamCount
};

static const char* const kVh4Names[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Deep", "Presence", "Master", "Cab Sim",
    "Channel",
    "CH1 Gain", "CH1 Bass", "CH1 Middle", "CH1 Treble", "CH1 Volume",
    "CH2 Gain", "CH2 Bass", "CH2 Middle", "CH2 Treble", "CH2 Volume",
    "CH3 Volume",
    "CH4 Gain", "CH4 Bass", "CH4 Middle", "CH4 Treble", "CH4 Volume",
};
static const char* const kVh4Symbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "deep", "presence", "master", "cabsim",
    "channel",
    "ch1_gain", "ch1_bass", "ch1_middle", "ch1_treble", "ch1_volume",
    "ch2_gain", "ch2_bass", "ch2_middle", "ch2_treble", "ch2_volume",
    "ch3_volume",
    "ch4_gain", "ch4_bass", "ch4_middle", "ch4_treble", "ch4_volume",
};
static const float kVh4Min[kParamCount] = {
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0, 0,0,0,0,0, 0, 0,0,0,0,0
};
static const float kVh4Max[kParamCount] = {
    1,1,1,1,1,1,1,1,1,
    1,1,1,1,1, 1,1,1,1,1, 1, 1,1,1,1,1
};
// Manual-insert defaults: the saturated-but-tight VH4 Mega rhythm — Gain past
// noon, tone centred, Deep/Presence flat, Master up.
static const float kVh4Def[kParamCount] = {
    // Legacy CH3 bank + global master section + selected channel.
    0.60f, 0.50f, 0.55f, 0.55f, 0.50f, 0.50f, 0.80f, 1.00f, 0.6667f,
    // CH1 clean
    0.38f, 0.55f, 0.50f, 0.58f, 0.72f,
    // CH2 crunch
    0.52f, 0.52f, 0.53f, 0.58f, 0.70f,
    // CH3 volume
    0.68f,
    // CH4 lead
    0.66f, 0.48f, 0.56f, 0.56f, 0.66f,
};

#endif // VH4_PARAMS_H
