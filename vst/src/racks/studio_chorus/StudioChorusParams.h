#ifndef STUDIO_CHORUS_PARAMS_H
#define STUDIO_CHORUS_PARAMS_H

// Rocksmith "Studio Chorus" rack -> Boss RCE-10 Chorus Ensemble.  The real unit
// is a 12-bit digital chorus with NE572 companding, pre/de-emphasis, R-2R
// conversion and stereo output expanders.  We keep the seven RS automation slots,
// but name them after the closest RCE-10 controls:
//   Rate         = modulation rate
//   Depth        = modulation depth
//   Effect Level = dual wet/output effect level
//   Low Cut      = extra RS low-cut before the real effect EQ
//   Effect EQ    = RCE-10 effect EQ/brightness
//   Stereo       = output A/B spread
//   Pre Delay    = RCE-10 pre-delay time
enum StudioChorusParamId {
    kRate = 0, kDepth, kMix, kLoFilter, kHiFilter, kStereo, kDelay, kParamCount
};

static const char* const kStudioChorusNames[kParamCount]   =
    { "Rate", "Depth", "Effect Level", "Low Cut", "Effect EQ", "Stereo", "Pre Delay" };
static const char* const kStudioChorusSymbols[kParamCount] =
    { "rate", "depth", "mix", "lofilter", "hifilter", "stereo", "delay" };

static const float kStudioChorusMin[kParamCount] = { 0,0,0,0,0,0,0 };
static const float kStudioChorusMax[kParamCount] = { 1,1,1,1,1,1,1 };
static const float kStudioChorusDef[kParamCount] =
    { 0.24f, 0.46f, 0.30f, 0.18f, 0.68f, 0.72f, 0.42f };

#endif // STUDIO_CHORUS_PARAMS_H
