/*
 * AlloyDistortion - component-guided Boss HM-2 style metal distortion.
 *
 * Reference: pedals/alloy distortion.pdf. Real panel controls are Dist,
 * Color Low, Color High and Level. Rocksmith Gain/Tone map onto Dist and
 * Color High for preset compatibility; Color Low and Level get static defaults.
 */
#include "DistrhoPlugin.hpp"
#include "AlloyDistortionParams.h"
#include "AlloyDistortionCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class AlloyDistortionPlugin : public Plugin
{
    alloydistortion::AlloyDistortionCore left;
    alloydistortion::AlloyDistortionCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setParams(params[kDist], params[kColorLow],
                       params[kColorHigh], params[kLevel]);
        right.setParams(params[kDist], params[kColorLow],
                        params[kColorHigh], params[kLevel]);
    }

public:
    AlloyDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAlloyDistortionDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AlloyDistortion"; }
    const char* getDescription() const override { return "HM-2 style heavy metal distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'l', 'D', 'y'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAlloyDistortionNames[index];
        parameter.symbol = kAlloyDistortionSymbols[index];
        parameter.ranges.min = kAlloyDistortionMin[index];
        parameter.ranges.max = kAlloyDistortionMax[index];
        parameter.ranges.def = kAlloyDistortionDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        const float sr = (float)newSampleRate;
        osL.reset();
        osR.reset();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        float ubL[kOS];
        float ubR[kOS];

        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(inL[i], ubL);
            osR.upsample(inR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = left.process(ubL[k]);
                ubR[k] = right.process(ubR[k]);
            }

            const float wetL = osL.downsample(ubL);
            const float wetR = osR.downsample(ubR);
            outL[i] = wetL;
            outR[i] = wetR;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyDistortionPlugin)
};

Plugin* createPlugin()
{
    return new AlloyDistortionPlugin();
}

END_NAMESPACE_DISTRHO
