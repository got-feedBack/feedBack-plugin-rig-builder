/*
 * Buzz-Tone - component-guided Captain Fuzzle / Maestro FZ-1A style fuzz.
 *
 * Reference: pedals/captain fuzzle.gif. The DSP core is deliberately split into
 * BuzzToneCore.h like the amp rework: RC coupling values, 1.5 V supply behavior,
 * and each 2N1305 germanium stage live in a DPF-free core that can be tested
 * offline. The wrapper only maps parameters, runs 2x oversampling around the
 * nonlinear core, and applies the real output Volume pot.
 */
#include "DistrhoPlugin.hpp"
#include "BuzzToneParams.h"
#include "BuzzToneCore.h"
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

static inline float staticFuzzMakeup(float fuzz)
{
    const float f = clamp01(fuzz);
    return 2.10f / (0.74f + 0.46f * f);
}

} // namespace

class BuzzTonePlugin : public Plugin
{
    buzztone::BuzzToneCore left;
    buzztone::BuzzToneCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setFuzz(params[kFuzz]);
        right.setFuzz(params[kFuzz]);
    }

public:
    BuzzTonePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBuzzToneDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        makeupL.setSampleRate(sr);
        makeupR.setSampleRate(sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Buzz-Tone"; }
    const char* getDescription() const override { return "1.5 V three-2N1305 germanium fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 't', 'n'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBuzzToneNames[index];
        parameter.symbol = kBuzzToneSymbols[index];
        parameter.ranges.min = kBuzzToneMin[index];
        parameter.ranges.max = kBuzzToneMax[index];
        parameter.ranges.def = kBuzzToneDef[index];
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
        const float vol = 1.6f * params[kVolume]; // default 0.60 ~= unity
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
            const float makeup = staticFuzzMakeup(params[kFuzz]);
            outL[i] = finalLimit(wetL * vol * makeup);
            outR[i] = finalLimit(wetR * vol * makeup);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BuzzTonePlugin)
};

Plugin* createPlugin()
{
    return new BuzzTonePlugin();
}

END_NAMESPACE_DISTRHO
