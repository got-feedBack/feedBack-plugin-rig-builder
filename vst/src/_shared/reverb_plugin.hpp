/*
 * Shared DSP body for the three bundled reverb racks. Each rack's Plugin.cpp
 * defines its voicing + identity, then includes this:
 *   #define REVERB_LABEL "StudioVerb"
 *   #define REVERB_DESC  "Studio hall reverb"
 *   #define REVERB_UID   d_cconst('R','B','V','b')
 *   #define REVERB_SIZE  1.00f   // comb size scale
 *   #define REVERB_DAMP  0.00f   // damping bias (+ = darker)
 *   #define REVERB_APFB  0.50f   // all-pass diffusion feedback
 *   #include "../_shared/reverb_plugin.hpp"
 */
#include "DistrhoPlugin.hpp"
#include "reverb_params.h"
#include "reverb_core.hpp"

#ifndef REVERB_WETMAX
#define REVERB_WETMAX 1.0f   // max wet/dry blend at Mix=100%; a rack can cap it lower for subtlety
#endif

START_NAMESPACE_DISTRHO

class ReverbRackPlugin : public Plugin {
    ReverbCore rv;
    float fParams[kParamCount];
    void recalc() { rv.setParams(fParams[kTime], fParams[kTone], fParams[kDepth], fParams[kMix]); }
public:
    ReverbRackPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kReverbDef[i];
        rv.setVoicing(REVERB_SIZE, REVERB_DAMP, REVERB_APFB);
        rv.setWetMax(REVERB_WETMAX);
        rv.setSampleRate((float)getSampleRate());
        recalc();
    }
protected:
    const char* getLabel()       const override { return REVERB_LABEL; }
    const char* getDescription() const override { return REVERB_DESC; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return REVERB_UID; }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kReverbNames[i]; p.symbol = kReverbSymbols[i];
        p.ranges.min = kReverbMin[i]; p.ranges.max = kReverbMax[i]; p.ranges.def = kReverbDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { rv.setSampleRate((float)r); recalc(); }
    // Drop the stored tail on host deactivate/reactivate (bypass toggle,
    // transport suspend) — otherwise up to seconds of old reverb replay.
    void  activate() override { rv.clear(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) rv.process(iL[i], iR[i], oL[i], oR[i]);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbRackPlugin)
};

Plugin* createPlugin() { return new ReverbRackPlugin(); }

END_NAMESPACE_DISTRHO
