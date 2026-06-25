/*
 * MarshallGuvnorPlus - component-guided Marshall GV-2/Guv'nor Plus drive.
 *
 * Reference: pedals/Marshall GV2_1.png and pedals/marshall gv2_2.gif. Real
 * controls are Gain, Bass, Mid, Treble, Deep and Volume.
 */
#include "DistrhoPlugin.hpp"
#include "MarshallGuvnorPlusParams.h"
#include "MarshallGuvnorPlusCore.h"
#include "../_shared/automakeup.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float finalLimit(float x)
{
    return std::tanh(0.98f * x);
}

} // namespace

class MarshallGuvnorPlusPlugin : public Plugin
{
    marshallguvnorplus::MarshallGuvnorPlusCore left;
    marshallguvnorplus::MarshallGuvnorPlusCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setBass(params[kBass]);
        right.setBass(params[kBass]);
        left.setMid(params[kMid]);
        right.setMid(params[kMid]);
        left.setTreble(params[kTreble]);
        right.setTreble(params[kTreble]);
        left.setDeep(params[kDeep]);
        right.setDeep(params[kDeep]);
    }

public:
    MarshallGuvnorPlusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMarshallGuvnorPlusDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        makeupL.setSampleRate(sr);
        makeupR.setSampleRate(sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarshallGuvnorPlus"; }
    const char* getDescription() const override { return "Marshall GV-2 style drive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'r', 'G', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarshallGuvnorPlusNames[index];
        parameter.symbol = kMarshallGuvnorPlusSymbols[index];
        parameter.ranges.min = kMarshallGuvnorPlusMin[index];
        parameter.ranges.max = kMarshallGuvnorPlusMax[index];
        parameter.ranges.def = kMarshallGuvnorPlusDef[index];
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
        makeupL.snap();
        makeupR.snap();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        const float sr = (float)newSampleRate;
        osL.reset();
        osR.reset();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        makeupL.setSampleRate(sr);
        makeupR.setSampleRate(sr);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        const float volume = 1.62f * params[kVolume];
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
            outL[i] = finalLimit(makeupL.process(inL[i], wetL) * volume);
            outR[i] = finalLimit(makeupR.process(inR[i], wetR) * volume);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarshallGuvnorPlusPlugin)
};

Plugin* createPlugin()
{
    return new MarshallGuvnorPlusPlugin();
}

END_NAMESPACE_DISTRHO
