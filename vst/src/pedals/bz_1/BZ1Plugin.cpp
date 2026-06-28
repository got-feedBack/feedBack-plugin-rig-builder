/*
 * BZ-1 - component-guided Boss FZ-3 / Aion Argent style silicon fuzz.
 *
 * Reference: pedals/Fuzz Was He.pdf. The real pedal has FUZZ, TONE and VOLUME
 * controls, so the plugin exposes those panel controls directly; Rocksmith's
 * Gain/Tone values are mapped onto Fuzz/Tone only as preset defaults.
 */
#include "DistrhoPlugin.hpp"
#include "BZ1Params.h"
#include "BZ1Core.h"
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

static inline float audioTaper(float v)
{
    const float x = clamp01(v);
    return x * x * (3.0f - 2.0f * x);
}

static inline float staticFuzzMakeup(float fuzz)
{
    const float f = clamp01(fuzz);
    return 1.20f / (0.68f + 0.42f * f);
}

} // namespace

class BZ1Plugin : public Plugin
{
    bz1::BZ1Core left;
    bz1::BZ1Core right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setFuzz(params[kFuzz]);
        right.setFuzz(params[kFuzz]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    BZ1Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBZ1Def[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BZ-1"; }
    const char* getDescription() const override { return "FZ-3 style silicon fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', '0', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBZ1Names[index];
        parameter.symbol = kBZ1Symbols[index];
        parameter.ranges.min = kBZ1Min[index];
        parameter.ranges.max = kBZ1Max[index];
        parameter.ranges.def = kBZ1Def[index];
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
        const float volume = 1.75f * audioTaper(params[kVolume]);
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
            outL[i] = finalLimit(wetL * volume * makeup);
            outR[i] = finalLimit(wetR * volume * makeup);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BZ1Plugin)
};

Plugin* createPlugin()
{
    return new BZ1Plugin();
}

END_NAMESPACE_DISTRHO
