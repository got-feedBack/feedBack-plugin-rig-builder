#ifndef STUDIO_CHORUS_PARAMS_H
#define STUDIO_CHORUS_PARAMS_H

// the game "Studio Chorus" rack -> Boss RCE-10 Chorus Ensemble.  ONLY the REAL
// RCE-10 front-panel pots are exposed (service notes 1986): RATE, DEPTH,
// EFFECT LEVEL, EQ, PRE DELAY.  The RCE-10 has NO low-cut pot and its A/B stereo
// image is fixed (not a knob), so the old RS-only "Low Cut" and "Stereo" params
// were removed.  RS knobs are remapped to these five in rs_knob_to_vst_param.json
// (Mix->Effect Level, HiFilter->Effect EQ, Delay->Pre Delay; RS LoFilter/Stereo
// are dropped).  Params apply BY NAME, so keeping these names means already-seeded
// tones keep working with NO re-seed.
enum StudioChorusParamId {
    kRate = 0, kDepth, kMix, kEq, kDelay, kParamCount
};

static const char* const kStudioChorusNames[kParamCount]   =
    { "Rate", "Depth", "Effect Level", "Effect EQ", "Pre Delay" };
static const char* const kStudioChorusSymbols[kParamCount] =
    { "rate", "depth", "mix", "hifilter", "delay" };

static const float kStudioChorusMin[kParamCount] = { 0,0,0,0,0 };
static const float kStudioChorusMax[kParamCount] = { 1,1,1,1,1 };
static const float kStudioChorusDef[kParamCount] =
    { 0.24f, 0.46f, 0.30f, 0.68f, 0.42f };

#endif // STUDIO_CHORUS_PARAMS_H
