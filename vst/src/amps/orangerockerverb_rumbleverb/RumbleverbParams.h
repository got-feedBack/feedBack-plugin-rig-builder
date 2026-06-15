#ifndef RUMBLEVERB_PARAMS_H
#define RUMBLEVERB_PARAMS_H

// "Citrus Rumbleverb 50" — Orange Rockerverb 50 MkII front panel, 1:1:
//   DIRTY channel : Gain · Bass · Middle · Treble · Volume (the high-gain channel;
//                   this is the one the game drives via Gain/Bass/Mid/Treble).
//   CLEAN channel : Clean Volume · Clean Bass · Clean Treble (the natural channel).
//   MASTER section: Reverb (valve spring) · Output (master into the 2x EL34 power).
//   Channel switch: Clean / Dirty.  Power/Standby + Full/Half output are cosmetic
//                   on the canvas face.
enum RumbleverbParamId {
    kGain = 0, kBass, kMiddle, kTreble, kVolume,            // DIRTY channel
    kCleanVolume, kCleanBass, kCleanTreble,                 // CLEAN channel
    kReverb, kOutput,                                       // MASTER section
    kChannel,                                               // switch (0=Clean, 1=Dirty)
    kParamCount
};

static const char* const kRumbleverbNames[kParamCount] = {
    "Gain", "Bass", "Middle", "Treble", "Volume",
    "Clean Volume", "Clean Bass", "Clean Treble",
    "Reverb", "Output", "Channel"
};
static const char* const kRumbleverbSymbols[kParamCount] = {
    "gain", "bass", "middle", "treble", "volume",
    "cleanvol", "cleanbass", "cleantreble",
    "reverb", "output", "channel"
};
static const float kRumbleverbMin[kParamCount] = { 0,0,0,0,0, 0,0,0, 0,0, 0 };
static const float kRumbleverbMax[kParamCount] = { 1,1,1,1,1, 1,1,1, 1,1, 1 };
// Dirty: Gain 0.5, EQ 0.5 flat, Volume 0.6; Clean: Vol 0.5, EQ 0.5; Reverb 0.2;
// Output 0.7; Channel = Dirty (1, the channel the game uses).
static const float kRumbleverbDef[kParamCount] = {
    0.50f, 0.50f, 0.50f, 0.50f, 0.60f,
    0.50f, 0.50f, 0.50f,
    0.20f, 0.70f, 1.00f
};

#endif // RUMBLEVERB_PARAMS_H
