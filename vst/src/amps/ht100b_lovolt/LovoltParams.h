#ifndef LOVOLT_PARAMS_H
#define LOVOLT_PARAMS_H

// "Lovolt 100" — Custom Hiwatt 100 (DR103) front panel, 1:1:
//   Normal Vol : the Normal channel input volume (12AX7 V1).
//   Bright Vol : the Bright channel volume (extra top via the bright cap).
//   Bass / Treble / Middle : the British (FMV) passive tone stack.
//   Presence   : power-amp NFB presence (top-end air).
//   Master Vol : output level into the 4x EL34 push-pull (~100 W, big clean
//                Hiwatt headroom).
enum LovoltParamId {
    kNormalVol = 0, kBrightVol, kBass, kTreble, kMiddle, kPresence, kMaster,
    kParamCount
};

static const char* const kLovoltNames[kParamCount] = {
    "Normal Vol", "Bright Vol", "Bass", "Treble", "Middle", "Presence", "Master Vol"
};
static const char* const kLovoltSymbols[kParamCount] = {
    "normalvol", "brightvol", "bass", "treble", "middle", "presence", "master"
};
static const float kLovoltMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kLovoltMax[kParamCount] = { 1,1,1,1,1,1,1 };
// Normal 0.5; Bright 0.3; tone stack flat 0.5; Presence 0.4; Master 0.7 ~ unity.
static const float kLovoltDef[kParamCount] = {
    0.50f, 0.30f, 0.50f, 0.50f, 0.50f, 0.40f, 0.70f
};

#endif // LOVOLT_PARAMS_H
