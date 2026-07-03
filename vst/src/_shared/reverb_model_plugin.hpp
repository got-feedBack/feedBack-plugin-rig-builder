#ifndef REVERB_MODEL_PLUGIN_HPP
#define REVERB_MODEL_PLUGIN_HPP

#include "DistrhoPlugin.hpp"
#include "reverb_params.h"
#include "studio_reverb_models.hpp"

#ifndef REVERB_WETMAX
#define REVERB_WETMAX 1.0f
#endif

#ifndef REVERB_VERSION_MAJOR
#define REVERB_VERSION_MAJOR 1
#endif
#ifndef REVERB_VERSION_MINOR
#define REVERB_VERSION_MINOR 1
#endif
#ifndef REVERB_VERSION_PATCH
#define REVERB_VERSION_PATCH 0
#endif

START_NAMESPACE_DISTRHO

class ReverbModelRackPlugin : public Plugin
{
    REVERB_MODEL_CLASS rv;
    float fParams[kParamCount];

    void recalc()
    {
        rv.setParams(fParams[kTime], fParams[kTone], fParams[kDepth], fParams[kMix]);
    }

public:
    ReverbModelRackPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kReverbDef[i];
        rv.setWetMax(REVERB_WETMAX);
        rv.setSampleRate((float)getSampleRate());
        recalc();
    }

protected:
    const char* getLabel() const override { return REVERB_LABEL; }
    const char* getDescription() const override { return REVERB_DESC; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(REVERB_VERSION_MAJOR, REVERB_VERSION_MINOR, REVERB_VERSION_PATCH); }
    int64_t getUniqueId() const override { return REVERB_UID; }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kReverbNames[i];
        p.symbol = kReverbSymbols[i];
        p.ranges.min = kReverbMin[i];
        p.ranges.max = kReverbMax[i];
        p.ranges.def = kReverbDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = v;
        recalc();
    }

    void sampleRateChanged(double r) override
    {
        rv.setSampleRate((float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i)
            rv.process(iL[i], iR[i], oL[i], oR[i]);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbModelRackPlugin)
};

Plugin* createPlugin()
{
    return new ReverbModelRackPlugin();
}

END_NAMESPACE_DISTRHO

#endif // REVERB_MODEL_PLUGIN_HPP
