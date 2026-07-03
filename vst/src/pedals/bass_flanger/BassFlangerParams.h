#ifndef BASS_FLANGER_PARAMS_H
#define BASS_FLANGER_PARAMS_H

// FL-3 is the bundled BF-2B-style bass flanger. The plugin exposes the real
// panel controls: Manual, Depth, Rate and Resonance. Rocksmith Rate/Depth/
// Filter/Mix are translated in data/rs_knob_to_vst_param.json.
enum BassFlangerParamId
{
    kManual = 0,
    kDepth,
    kRate,
    kRes,
    kParamCount
};

static const char* const kBassFlangerNames[kParamCount] = {
    "Manual",
    "Depth",
    "Rate",
    "Res",
};

static const char* const kBassFlangerSymbols[kParamCount] = {
    "manual",
    "depth",
    "rate",
    "res",
};

static const float kBassFlangerMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassFlangerMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassFlangerDef[kParamCount] = { 0.45f, 0.46f, 0.24f, 0.28f };

#endif // BASS_FLANGER_PARAMS_H
