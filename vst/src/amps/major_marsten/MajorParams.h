#ifndef MAJOR_PARAMS_H
#define MAJOR_PARAMS_H

/*
 * MARSTEN MAJOR = Marshall Major 200W ("The Pig", 1967) — the FULL front panel,
 * from the local schematics (Marshall-Major-200W-Schematic.pdf +
 * Marshall-Major-1966-200W-PA-Schematic.pdf). Parody brand "Marsten" (same
 * family as the Plexi / JCM800 / DSL100 / Silver Jubilee). The in-app face must
 * never read "Marshall".
 *
 * What makes the Major a Major (vs the Plexi):
 *   - 4x KT88 power tubes (not EL34) at ~585-660V B+ with a cold ~-81V fixed
 *     bias -> 200W and HUGE clean headroom, tight and "hi-fi", late/smooth
 *     breakup instead of the Plexi's early EL34 crunch.
 *   - a 12AU7 (ECC82) long-tail phase inverter (not the Plexi's 12AX7) -> a
 *     cleaner, higher-headroom splitter that only hardens when really pushed.
 *   - V1 channel volumes feeding a 470k mixer, then ECC83 V2 and the passive
 *     250pF/22nF/22nF stack before the ECC82 splitter.
 * It's the Ritchie Blackmore / Deep Purple "loud clean platform + booster"
 * amp, NOT a distortion machine.
 *
 * NON-MASTER-VOLUME amp: the two Volume pots ARE the gain (like the Plexi).
 * Panel (1:1, left->right): PRESENCE, BASS, MIDDLE, TREBLE, VOLUME I, VOLUME II.
 * Input jumper (Bright / jumpered / Normal) mirrors the 4-input front end. Cab
 * Sim is the internal 4x12 fallback voice (auto-muted by the host when a real
 * cab/IR is in the chain).
 *
 * EXTRA gear (not mapped to any RS song) — panel is the real Major.
 */
enum MajorParamId
{
    kPresence = 0,   // PRESENCE (power-amp NFB high-shelf)
    kBass,           // BASS   Marshall tone stack (1M)
    kMiddle,         // MIDDLE Marshall tone stack (25k)
    kTreble,         // TREBLE Marshall tone stack (250k / 250pF)
    kVolume1,        // VOLUME I  — Bright/High-Treble channel (drive)
    kVolume2,        // VOLUME II — Normal channel (drive)
    kInput,          // input cable: Bright(0) / Both-jumpered(0.5) / Normal(1)
    kCabSim,         // fallback 4x12 voice: 0 = amp-only, 1 = internal cab sim
    kParamCount
};

static const char* const kMajorNames[kParamCount] = {
    "Presence", "Bass", "Middle", "Treble", "Volume I", "Volume II", "Input", "Cab Sim",
};
static const char* const kMajorSymbols[kParamCount] = {
    "presence", "bass", "middle", "treble", "volume1", "volume2", "input", "cabsim",
};
static const float kMajorMin[kParamCount] = { 0,0,0,0,0,0,0,0 };
static const float kMajorMax[kParamCount] = { 1,1,1,1,1,1,1,1 };
// Manual-insert defaults: the Major's signature LOUD-CLEAN/edge-of-breakup off
// Volume I (its headroom keeps it clean well past noon), tone centred, Volume II
// down, input JUMPERED so raising Volume II by hand blends the Normal channel.
static const float kMajorDef[kParamCount] = {
    0.50f, 0.50f, 0.55f, 0.60f, 0.68f, 0.00f, 0.50f, 1.00f,
};

#endif // MAJOR_PARAMS_H
