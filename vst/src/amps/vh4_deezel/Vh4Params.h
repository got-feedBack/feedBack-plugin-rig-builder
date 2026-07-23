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
 * ── DESIGNED FOR EXPANSION ──
 * The core (Vh4Core) is channel-parameterised: it carries a `channel` index and
 * a voiceChannel() switch, with Ch3 ("Mega") filled in now and Ch1 (Clean),
 * Ch2 (Crunch) and Ch4 (Lead) reserved as future cases. When those land, add a
 * kChannel selector + the per-channel Gain/Bass/Mid/Treble/Volume banks AFTER
 * kCabSim (so these indices never shift), and the global Deep/Presence/Master
 * become shared. For now the panel is the single-channel Ch3 layout.
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
    kParamCount
};

static const char* const kVh4Names[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Deep", "Presence", "Master", "Cab Sim",
    "Channel",
};
static const char* const kVh4Symbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "deep", "presence", "master", "cabsim",
    "channel",
};
static const float kVh4Min[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kVh4Max[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: the saturated-but-tight VH4 Mega rhythm — Gain past
// noon, tone centred, Deep/Presence flat, Master up.
static const float kVh4Def[kParamCount] = {
    0.60f, 0.50f, 0.55f, 0.55f, 0.50f, 0.50f, 0.55f, 1.00f, 0.6667f,   // default Ch3 Mega
};

#endif // VH4_PARAMS_H
