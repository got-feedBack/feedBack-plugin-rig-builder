#ifndef RUMBLE_PARAMS_H
#define RUMBLE_PARAMS_H

// "Bender Rumble 800" — Fender Rumble Bass (1995 all-tube head) front panel:
//   Gain   : drives the 12AX7 input stage (clean bass voice, grinds when cranked).
//   Bass   : passive tone-stack low shelf (~75 Hz).
//   Middle : Fender mid control (gentle ~500 Hz peak/dip).
//   Treble : passive tone-stack high shelf (~4 kHz).
//   Bright : bright-cap high lift across the volume (extra top/clank).
//   Master : output level into the 6x 6550WA push-pull (~300 W, big headroom).
enum RumbleParamId {
    kGain = 0, kBass, kMiddle, kTreble, kMaster,   // knobs
    kBright,                                        // switch
    kParamCount
};

static const char* const kRumbleNames[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Master", "Bright"
};
static const char* const kRumbleSymbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "master", "bright"
};
static const float kRumbleMin[kParamCount] = { 0,0,0,0,0, 0 };
static const float kRumbleMax[kParamCount] = { 1,1,1,1,1, 1 };
// Tone knobs flat at 0.5; Gain 0.5; Master 0.7 ~ unity; Bright off.
static const float kRumbleDef[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.50f, 0.70f, 0.00f
};

#endif // RUMBLE_PARAMS_H
