/*
 * BassDistortion - Pro Co RAT style model for Bass_Pedal_BassDistortion.
 *
 * Schematic blocks modeled here: input coupling, compensated LM308 high-gain
 * stage, anti-parallel 1N4148 clipping diodes, passive RAT Filter, JFET-ish
 * output buffer, and the real Distortion/Filter/Volume control set.
 */
#include "DistrhoPlugin.hpp"
#include "BassDistortionParams.h"
#include "RatCore.h"
#include "../_shared/oversampler.hpp"

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

class BassDistortionPlugin : public Plugin
{
    rat::RatCore L, R;
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    float fParams[kParamCount];

    void recalc()
    {
        L.setParams(fParams[kDistortion], fParams[kFilter], fParams[kVolume]);
        R.setParams(fParams[kDistortion], fParams[kFilter], fParams[kVolume]);
    }

public:
    BassDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kBassDistortionDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(kOS * sr);
        R.setSampleRate(kOS * sr);
        recalc();
    }

protected:
    const char* getLabel() const override { return "BassDistortion"; }
    const char* getDescription() const override { return "Pro Co RAT distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'D', 's'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassDistortionNames[i];
        p.symbol = kBassDistortionSymbols[i];
        p.ranges.min = kBassDistortionMin[i];
        p.ranges.max = kBassDistortionMax[i];
        p.ranges.def = kBassDistortionDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i < (uint32_t)kParamCount)
        {
            fParams[i] = clamp01(v);
            recalc();
        }
    }

    void sampleRateChanged(double r) override
    {
        osL.reset();
        osR.reset();
        L.setSampleRate(kOS * (float)r);
        R.setSampleRate(kOS * (float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        float ubL[kOS];
        float ubR[kOS];
        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(iL[i], ubL);
            osR.upsample(iR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = L.process(ubL[k]);
                ubR[k] = R.process(ubR[k]);
            }
            oL[i] = osL.downsample(ubL);
            oR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassDistortionPlugin)
};

Plugin* createPlugin() { return new BassDistortionPlugin(); }

END_NAMESPACE_DISTRHO
