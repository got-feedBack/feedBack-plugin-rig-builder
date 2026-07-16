/*
 * StandardDistortion - component-guided Boss DS-1 style distortion.
 *
 * Reference: pedals/standard distortion.pdf. The real panel is DIST, TONE and
 * LEVEL; Rocksmith Gain/Tone are mapped onto Dist/Tone for preset compatibility.
 */
#include "DistrhoPlugin.hpp"
#include "StandardDistortionParams.h"
#include "StandardDistortionCore.h"
#include "../../_shared/oversampler.hpp"

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

class StandardDistortionPlugin : public Plugin
{
    standarddistortion::StandardDistortionCore left;
    standarddistortion::StandardDistortionCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setParams(params[kDist], params[kTone], params[kLevel]);
        right.setParams(params[kDist], params[kTone], params[kLevel]);
    }

public:
    StandardDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStandardDistortionDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StandardDistortion"; }
    const char* getDescription() const override { return "DS-1 style standard distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 's', '1', 'D'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kStandardDistortionNames[index];
        parameter.symbol = kStandardDistortionSymbols[index];
        parameter.ranges.min = kStandardDistortionMin[index];
        parameter.ranges.max = kStandardDistortionMax[index];
        parameter.ranges.def = kStandardDistortionDef[index];
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

            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandardDistortionPlugin)
};

Plugin* createPlugin()
{
    return new StandardDistortionPlugin();
}

END_NAMESPACE_DISTRHO
