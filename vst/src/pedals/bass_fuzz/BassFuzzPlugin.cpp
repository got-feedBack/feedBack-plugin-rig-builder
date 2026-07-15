/*
 * BassFuzz - EHX Bass Big Muff Pi style model for Bass_Pedal_BassFuzz.
 *
 * Reworked to the same recipe as the other fuzzes (Big Buzz / Buzz-Tone / …):
 *   - circuit model lives DPF-free in BassFuzzCore.h,
 *   - the nonlinear core runs OVERSAMPLED to tame clipping aliasing,
 *   - a measured static curve keeps the Sustain sweep usable without changing
 *     note envelopes,
 *   - the real Volume pot is applied after circuit calibration.
 */
#include "DistrhoPlugin.hpp"
#include "BassFuzzParams.h"
#include "BassFuzzCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float finalLimit(float x)
{
    if (x > 1.0f)
        return 1.0f - std::exp(-(x - 1.0f));
    if (x < -1.0f)
        return -1.0f + std::exp(x + 1.0f);
    return x;
}

static inline float audioTaper(float v)
{
    const float x = clamp01(v);
    return std::pow(x, 2.15f);
}

static inline float staticFuzzMakeup(float sustain, float bassDry)
{
    const float s = clamp01(sustain);
    const float correctionDb = -16.5f * (1.0f - std::exp(-4.0f * s));
    const float sustainGain = std::pow(10.0f, correctionDb / 20.0f);
    const float mode = quantize3(bassDry);
    // The switched Bass Boost and Dry paths do not follow Normal's gain curve.
    // Static mode calibration keeps switch changes close in level without an
    // envelope follower that would erase their attack and sustain differences.
    const float modeGain = mode > 0.5f ? 0.225f + 0.66f * (1.0f - std::exp(-3.0f * s))
                                       : (mode > 0.0f ? 0.64f : 1.0f);
    return sustainGain * modeGain;
}

} // namespace

class BassFuzzPlugin : public Plugin
{
    bassfuzz::BassBigMuffCore left;
    bassfuzz::BassBigMuffCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float fParams[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setParams(fParams[kSustain], fParams[kTone], fParams[kBassDry]);
        right.setParams(fParams[kSustain], fParams[kTone], fParams[kBassDry]);
    }

public:
    BassFuzzPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kBassFuzzDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassFuzz"; }
    const char* getDescription() const override { return "Bass Big Muff Pi fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 5, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'F', 'z'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFuzzNames[i];
        p.symbol = kBassFuzzSymbols[i];
        p.ranges.min = kBassFuzzMin[i];
        p.ranges.max = kBassFuzzMax[i];
        p.ranges.def = kBassFuzzDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = (i == (uint32_t)kBassDry) ? quantize3(v) : clamp01(v);
        applyAll();
    }

    void sampleRateChanged(double r) override
    {
        const float sr = (float)r;
        osL.reset();
        osR.reset();
        left.reset();
        right.reset();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        const float volume = 2.0f * audioTaper(fParams[kVolume]);
        const float makeup = staticFuzzMakeup(fParams[kSustain], fParams[kBassDry]);
        float ubL[kOS];
        float ubR[kOS];

        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(iL[i], ubL);
            osR.upsample(iR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = left.process(ubL[k]);
                ubR[k] = right.process(ubR[k]);
            }
            const float wetL = osL.downsample(ubL);
            const float wetR = osR.downsample(ubR);

            oL[i] = finalLimit(wetL * volume * makeup);
            oR[i] = finalLimit(wetR * volume * makeup);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzPlugin)
};

Plugin* createPlugin() { return new BassFuzzPlugin(); }

END_NAMESPACE_DISTRHO
