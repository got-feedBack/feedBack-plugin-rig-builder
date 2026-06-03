#ifndef STUDIO_CHORUS_PARAMS_H
#define STUDIO_CHORUS_PARAMS_H

// Rocksmith "Studio Chorus" rack -> Boss RCE-10 Chorus Ensemble. A lush stereo
// BBD-style chorus: two modulated voices per channel, the L/R LFOs spread by a
// Stereo control, with low/high filtering shaping the wet voice. Seven knobs:
//   Rate     = LFO speed (RS stores this in Hz, ~0.2 .. 2.5)
//   Depth    = modulation depth (sweep amount)
//   Mix      = wet/dry blend (typically subtle on this unit)
//   LoFilter = low-cut on the wet (high-pass; higher = thinner)
//   HiFilter = high-cut on the wet (low-pass; higher = brighter)
//   Stereo   = L/R LFO spread / width
//   Delay    = base delay time the modulation rides on
enum StudioChorusParamId {
    kRate = 0, kDepth, kMix, kLoFilter, kHiFilter, kStereo, kDelay, kParamCount
};

static const char* const kStudioChorusNames[kParamCount]   =
    { "Rate", "Depth", "Mix", "Lo Filter", "Hi Filter", "Stereo", "Delay" };
static const char* const kStudioChorusSymbols[kParamCount] =
    { "rate", "depth", "mix", "lofilter", "hifilter", "stereo", "delay" };

static const float kStudioChorusMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kStudioChorusMax[kParamCount] = { 1,1,1,1,1,1,1 };
static const float kStudioChorusDef[kParamCount] =
    { 0.30f, 0.60f, 0.20f, 0.30f, 0.60f, 0.60f, 0.40f };

#endif // STUDIO_CHORUS_PARAMS_H
