/*
 * Big Buzz - component-guided V1 Big Muff / triangle-era silicon fuzz.
 *
 * Reference: pedals/buzz 2.jpg. The VST exposes the real panel controls:
 * Sustain, Tone and Volume. Rocksmith Gain/Tone are only preset inputs mapped
 * onto Sustain/Tone; Volume stays at the default unless edited by hand.
 */
#include "DistrhoPlugin.hpp"
#include "BigBuzzParams.h"
#include "BigBuzzCore.h"
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
    if (x > 1.0f)
        return 1.0f - std::exp(-(x - 1.0f));
    if (x < -1.0f)
        return -1.0f + std::exp(x + 1.0f);
    return x;
}

static inline float staticFuzzMakeup(float sustain, float tone)
{
    const float s = clamp01(sustain);
    const float t = clamp01(tone);
    // The real 100 k Sustain pot raises the signal feeding Q3. Keep that
    // audible level/sustain rise instead of cancelling it with inverse makeup.
    // Reference sweep: most of the audible rise occurs in the first half of
    // the linear 100 k pot, then the diode stages compress the upper half.
    const float base = 0.500f + 0.324f * s - 0.120f * s * s;
    const float su = clamp01(2.0f * s);
    const float compressed = su * su * (3.0f - 2.0f * su);
    const float loadedToneGain = 1.0f + compressed * (0.06f + 0.55f * t - 0.22f * t * t);
    const float recoveryGain = 1.0f + 0.80f * t + 0.06f * t * t;
    return base * loadedToneGain * recoveryGain;
}

} // namespace

class BigBuzzPlugin : public Plugin
{
    bigbuzz::BigBuzzCore left;
    bigbuzz::BigBuzzCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setSustain(params[kSustain]);
        right.setSustain(params[kSustain]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    BigBuzzPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBigBuzzDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Big Buzz"; }
    const char* getDescription() const override { return "V1 Big Muff style silicon fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 'T', '2'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBigBuzzNames[index];
        parameter.symbol = kBigBuzzSymbols[index];
        parameter.ranges.min = kBigBuzzMin[index];
        parameter.ranges.max = kBigBuzzMax[index];
        parameter.ranges.def = kBigBuzzDef[index];
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
            const float makeup = staticFuzzMakeup(params[kSustain], params[kTone]);
            outL[i] = finalLimit(wetL * volume * makeup);
            outR[i] = finalLimit(wetR * volume * makeup);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigBuzzPlugin)
};

Plugin* createPlugin()
{
    return new BigBuzzPlugin();
}

END_NAMESPACE_DISTRHO
