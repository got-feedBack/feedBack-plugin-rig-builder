#ifndef IRONHEART_PARAMS_H
#define IRONHEART_PARAMS_H

/*
 * RANEY IRONHEART = Laney Ironheart IRT60H (2x 6L6, 4x ECC83). Modelled from
 * the official service schematic (amps/Laney Ironheart/laney_irt60h_sm.pdf)
 * and calibrated per channel against the user's Plugin Alliance-style
 * references (test logic/ironheart/, Brit DI, amp-only).
 *
 * Real path: TL072 buffer -> op-amp PRE-BOOST (VR2 100K + 220 pF) -> V1A
 * (100K plate, 1K5||1uF cathode) -> per channel:
 *   LEAD:   CH2 GAIN 220K log -> V1B -> V2A -> V2B -> V3B (low ~140 V plates)
 *   RHYTHM: divider -> RHYTHM DRIVE 220K -> V3A both halves
 *   CLEAN:  divider (220K/4K7 + 470 pF) -> CLEAN VOLUME 220K -> V3A one half
 * -> TMB stack (Treble 250K/470 pF, slope 47K, Bass 250K/22n+100n, Mid
 * 25K/22n) -> channel VOLUME 1M log -> LTP PI (82K/100K) -> TONE (dual 220K
 * post-PI tilt) -> WATTS (dual 220K drive attenuator, "<1W..MAX") -> 2x 6L6
 * (bias-selectable EL34) ; DYNAMICS = the schematic's ENHANCE (NFB depth,
 * 220K + 6n8/39K). The FV-1 digital reverb is NOT modelled (racks do reverb).
 *
 * EXTRA gear (not mapped to any RS song) - the song-mapped Laney stays AOR50.
 */
enum IronheartParamId
{
    kGain = 0,      // GAIN of the active channel (Clean Volume / Rhythm Drive / CH2 Gain)
    kBass,          // BASS   250K log
    kMiddle,        // MIDDLE 25K lin
    kTreble,        // TREBLE 250K lin
    kVolume,        // channel VOLUME (1M log, pre-PI)
    kDynamics,      // DYNAMICS (ENHANCE): power NFB depth - loose lows / feel
    kTone,          // TONE: dual 220K post-PI tilt
    kWatts,         // WATTS: dual 220K PI-drive attenuator (<1W .. 60W)
    kCabSim,        // fallback 4x12 voice: 0 = amp-only, 1 = internal cab sim
    kChannel,       // 0 / 0.5 / 1 -> Clean / Rhythm / Lead
    kBoost,         // PRE-BOOST level (op-amp, VR2 100K)
    kBoostOn,       // PRE-BOOST toggle
    kParamCount
};

static const char* const kIronheartNames[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Volume", "Dynamics", "Tone", "Watts",
    "Cab Sim", "Channel", "Boost", "Boost On",
};
static const char* const kIronheartSymbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "volume", "dynamics", "tone", "watts",
    "cabsim", "channel", "boost", "booston",
};
static const float kIronheartMin[kParamCount] = { 0,0,0,0,0,0,0,0,0,0,0,0 };
static const float kIronheartMax[kParamCount] = { 1,1,1,1,1,1,1,1,1,1,1,1 };
// Defaults: Rhythm channel, controls at noon, watts full, boost off.
static const float kIronheartDef[kParamCount] = {
    0.55f, 0.50f, 0.50f, 0.50f, 0.60f, 0.50f, 0.50f, 1.00f, 1.00f, 0.50f, 0.50f, 0.00f,
};

#endif // IRONHEART_PARAMS_H
