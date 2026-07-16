/*
 * Super-Buzz - component-guided Univox Super-Fuzz style octave fuzz.
 *
 * Reference: pedals/buzz 1.gif. The real controls are EXPANDER 50 kB, a two
 * position TONE switch, and BALANCE 50 kB. Rocksmith Gain/Tone are mapped onto
 * Expander/Tone SW for preset compatibility.
 */
#include "DistrhoPlugin.hpp"
#include "SuperBuzzParams.h"
#include "SuperBuzzCore.h"
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
    const float a = std::fabs(x);
    if (a <= 0.95f)
        return x;
    const float limited = 0.95f + 0.05f * std::tanh((a - 0.95f) / 0.05f);
    return std::copysign(limited, x);
}

static inline float staticFuzzMakeup(float expander)
{
    const float e = clamp01(expander);
    const float u = clamp01(2.0f * e);
    const float compressed = u * u * (3.0f - 2.0f * u);
    return 1.0f - 0.40f * compressed;
}

static inline float balancePotGain(float balance)
{
    // Real BALANCE is the 50 kB output-level pot, not a clean/fuzz blend.
    // Normalize its audio taper at the curated default so the full travel is
    // audible without changing existing song levels around 0.62.
    const float b = clamp01(balance);
    const float reference = std::pow(kSuperBuzzDef[kBalance], 1.7f);
    return std::pow(b, 1.7f) / reference;
}

} // namespace

class SuperBuzzPlugin : public Plugin
{
    superbuzz::SuperBuzzCore left;
    superbuzz::SuperBuzzCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setExpander(params[kExpander]);
        right.setExpander(params[kExpander]);
        left.setToneSwitch(params[kToneSwitch]);
        right.setToneSwitch(params[kToneSwitch]);
    }

public:
    SuperBuzzPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSuperBuzzDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Super-Buzz"; }
    const char* getDescription() const override { return "Univox Super-Fuzz style octave fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 4, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 'O', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kToneSwitch)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kSuperBuzzNames[index];
        parameter.symbol = kSuperBuzzSymbols[index];
        parameter.ranges.min = kSuperBuzzMin[index];
        parameter.ranges.max = kSuperBuzzMax[index];
        parameter.ranges.def = kSuperBuzzDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kToneSwitch) ? (value >= 0.5f ? 1.0f : 0.0f)
                                                         : clamp01(value);
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
        const float balance = balancePotGain(params[kBalance]);
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
            const float makeup = staticFuzzMakeup(params[kExpander]);
            outL[i] = finalLimit(wetL * balance * makeup);
            outR[i] = finalLimit(wetR * balance * makeup);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuperBuzzPlugin)
};

Plugin* createPlugin()
{
    return new SuperBuzzPlugin();
}

END_NAMESPACE_DISTRHO
