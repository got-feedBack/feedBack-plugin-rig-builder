#ifndef AOR50_PARAMS_H
#define AOR50_PARAMS_H

/*
 * RANEY AOR50 = Laney AOR 50 "Pro Tube Lead" (A50 Series II) — the FULL front
 * panel, 1:1, from the local schematic (Laney_aor50_series2.pdf). Parody brand
 * "Raney"; the face must never read "Laney".
 *
 * Two footswitchable channels off an ECC83 preamp into an EL34 power amp:
 *   CHANNEL ONE : Preamp Volume + Master Volume (Pull-Bright)        — clean/rhythm
 *   AOR CHANNEL : Preamp Volume (Pull-AOR-On) + Master (Pull-Bright) — cascaded lead
 *   shared      : Bass (Pull-Deep), Middle (Pull-Boost), Treble, Presence (NFB)
 *
 * the game mapping (rs_knob_to_vst_param.json): the single Gain knob DRIVES THE
 * CHANNEL — low Gain = Channel One (clean), high Gain morphs into the AOR lead
 * (matching the gain_variants clean/crunch/dist split). Bass/Mid/Treble -> tone
 * stack, Pres -> Presence. Per-channel preamp/master volumes + the pull
 * switches sit at musical defaults (_static) and stay editable by hand.
 */
enum AOR50ParamId
{
    kChannel = 0,    // CHANNEL ONE(0) / AOR(1)  (the "Pull AOR On")   [RS Gain morph]
    kAorPreamp,      // AOR Preamp Volume (lead gain/drive)
    kAorMaster,      // AOR Master Volume
    kAorBright,      // AOR Master Pull-Bright
    kCh1Preamp,      // CHANNEL ONE Preamp Volume
    kCh1Master,      // CHANNEL ONE Master Volume
    kCh1Bright,      // CHANNEL ONE Master Pull-Bright
    kBass,           // Bass                                           [RS Bass]
    kMiddle,         // Middle                                         [RS Mid]
    kTreble,         // Treble                                         [RS Treble]
    kDeep,           // Bass Pull-Deep (low-end boost)
    kMidBoost,       // Middle Pull-Boost (mid lift)
    kPresence,       // Presence (power-amp NFB)                       [RS Pres]
    kParamCount
};

static const char* const kAOR50Names[kParamCount] = {
    "Channel", "AOR Preamp", "AOR Master", "AOR Bright",
    "Ch1 Preamp", "Ch1 Master", "Ch1 Bright",
    "Bass", "Middle", "Treble", "Deep", "Mid Boost", "Presence",
};

static const char* const kAOR50Symbols[kParamCount] = {
    "channel", "aorpreamp", "aormaster", "aorbright",
    "ch1preamp", "ch1master", "ch1bright",
    "bass", "middle", "treble", "deep", "midboost", "presence",
};

static const float kAOR50Min[kParamCount] = { 0,0,0,0, 0,0,0, 0,0,0,0,0,0 };
static const float kAOR50Max[kParamCount] = { 1,1,1,1, 1,1,1, 1,1,1,1,1,1 };
// Manual-insert defaults: AOR (lead) channel — the signature voice — at a
// moderate lead gain; Channel One set clean; tone stack centred-ish; pull
// switches off; presence mid.
static const float kAOR50Def[kParamCount] = {
    1.00f, 0.60f, 0.50f, 0.00f,   // Channel=AOR, AOR Preamp/Master, AOR Bright off
    0.50f, 0.50f, 0.00f,          // Ch1 Preamp/Master, Ch1 Bright off
    0.55f, 0.50f, 0.60f, 0.00f, 0.00f,   // Bass, Middle, Treble, Deep off, Mid Boost off
    0.50f,                        // Presence
};

#endif // AOR50_PARAMS_H
