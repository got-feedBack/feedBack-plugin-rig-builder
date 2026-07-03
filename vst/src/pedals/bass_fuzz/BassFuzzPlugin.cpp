/*
 * BassFuzz - EHX Bass Big Muff Pi style model for Bass_Pedal_BassFuzz.
 *
 * Reworked to the same recipe as the other fuzzes (Big Buzz / Buzz-Tone / …):
 *   - circuit model lives DPF-free in BassFuzzCore.h,
 *   - the nonlinear core runs OVERSAMPLED to tame clipping aliasing,
 *   - RBAutoMakeup loudness-locks the wet output to the dry DI so the Sustain
 *     knob changes the FUZZ, not the volume — this is what stops the synth-bass
 *     tone from blasting over the song,
 *   - the real Volume pot is applied AFTER makeup (defaults to ~unity).
 */
#include "DistrhoPlugin.hpp"
#include "BassFuzzParams.h"
#include "BassFuzzCore.h"
#include "../../_shared/oversampler.hpp"
#include "../../_shared/automakeup.hpp"
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
    return std::tanh(0.98f * x);
}

} // namespace

class BassFuzzPlugin : public Plugin
{
    bassfuzz::BassBigMuffCore left;
    bassfuzz::BassBigMuffCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
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
        makeupL.setSampleRate(sr);
        makeupR.setSampleRate(sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassFuzz"; }
    const char* getDescription() const override { return "Bass Big Muff Pi fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
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
        // Re-level fast while a knob is moving (no steady-state bias).
        makeupL.snap();
        makeupR.snap();
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
        makeupL.setSampleRate(sr);
        makeupR.setSampleRate(sr);
        applyAll();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        // Volume pot applied AFTER makeup. 1.35 (was 1.6) seats the pedal ~1.5 dB
        // under the DI at the default so the synth-bass fuzz sits just below the
        // mix instead of right at it.
        const float volume = 1.0f * fParams[kVolume];
        float ubL[kOS];
        float ubR[kOS];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float dryL = iL[i];
            const float dryR = iR[i];

            osL.upsample(dryL, ubL);
            osR.upsample(dryR, ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = left.process(ubL[k]);
                ubR[k] = right.process(ubR[k]);
            }
            const float wetL = osL.downsample(ubL);
            const float wetR = osR.downsample(ubR);

            // Loudness-lock wet→dry, then the real Volume pot, then a soft peak limit.
            oL[i] = finalLimit(makeupL.process(dryL, wetL) * volume);
            oR[i] = finalLimit(makeupR.process(dryR, wetR) * volume);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzPlugin)
};

Plugin* createPlugin() { return new BassFuzzPlugin(); }

END_NAMESPACE_DISTRHO
