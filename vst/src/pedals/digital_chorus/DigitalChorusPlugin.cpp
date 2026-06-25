/*
 * CH-5 - Boss CE-5 style digital chorus.
 *
 * Reference: pedals/digital chorus.png. Component-guided blocks: 2SK879Y
 * input switching/buffer, NJM022/M5223 filters, ES6028 digital delay core,
 * Lo/Hi filter controls and effect-level output blend.
 */
#include "DistrhoPlugin.hpp"
#include "DigitalChorusParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class DigitalChorusCore
{
    float sampleRate = 48000.0f;
    float level = kDigitalChorusDef[kLevel];
    float rate = kDigitalChorusDef[kRate];
    float depth = kDigitalChorusDef[kDepth];
    float loFilter = kDigitalChorusDef[kLoFilter];
    float hiFilter = kDigitalChorusDef[kHiFilter];
    float phaseOffset = 0.0f;
    float lfoPhase = 0.0f;
    float pre = 0.0f;

    rbmod::DelayBuffer delay;
    rbmod::HighPass inputHp;
    rbmod::HighPass wetHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass preEmphasis;
    rbmod::LowPass wetLp;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.055f + 6.60f * std::pow(rbmod::clamp01(rate), 2.10f);
    }

    void updateFilters()
    {
        const float lo = rbmod::smoothstep(loFilter);
        const float hi = rbmod::smoothstep(hiFilter);
        inputHp.setHz(22.0f, sampleRate);
        inputLp.setHz(13200.0f, sampleRate);
        wetHp.setHz(34.0f + 780.0f * lo, sampleRate);
        wetLp.setHz(12500.0f - 8700.0f * hi, sampleRate);
        preEmphasis.setHz(2300.0f + 5200.0f * (1.0f - hi), sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resizeForMs(sampleRate, 58.0f);
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed) { noise.seed(seed); }
    void setPhaseOffset(float v) { phaseOffset = v - std::floor(v); }

    void reset()
    {
        delay.reset();
        inputHp.reset();
        wetHp.reset();
        inputLp.reset();
        preEmphasis.reset();
        wetLp.reset();
        lfoPhase = phaseOffset;
        pre = 0.0f;
    }

    void setLevel(float v) { level = rbmod::clamp01(v); }
    void setRate(float v) { rate = rbmod::clamp01(v); }
    void setDepth(float v) { depth = rbmod::clamp01(v); }
    void setLoFilter(float v) { loFilter = rbmod::clamp01(v); updateFilters(); }
    void setHiFilter(float v) { hiFilter = rbmod::clamp01(v); updateFilters(); }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float p = lfoPhase + phaseOffset;
        const float tri = 4.0f * std::fabs((p - std::floor(p)) - 0.5f) - 1.0f;
        const float sine = std::sin(rbmod::kTwoPi * p);
        const float lfoA = 0.58f * sine - 0.42f * tri;
        const float lfoB = std::sin(rbmod::kTwoPi * (p + 0.31f));

        const float d = std::pow(rbmod::clamp01(depth), 1.80f);
        const float baseMs = 14.2f + 3.5f * (1.0f - d);
        const float widthMs = 0.10f + 8.10f * d;
        float delayA = baseMs + widthMs * lfoA;
        float delayB = baseMs * 1.38f + widthMs * 0.52f * lfoB;
        delayA = rbmod::clamp(delayA, 3.0f, 34.0f);
        delayB = rbmod::clamp(delayB, 4.0f, 46.0f);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        pre = preEmphasis.process(x);
        x = 0.90f * x + 0.10f * (x - pre);
        x = rbmod::softClip(x * 1.015f);

        const float tapA = delay.read(delayA * 0.001f * sampleRate);
        const float tapB = delay.read(delayB * 0.001f * sampleRate);
        delay.write(x);

        // Digital core is cleaner than a BBD, but the ES6028 path is not
        // perfectly transparent. Keep only a very small shaped quantization.
        float wet = 0.72f * tapA + 0.28f * tapB;
        wet += noise.next() * (0.00005f + 0.00020f * d);
        wet = wetLp.process(wetHp.process(wet));
        wet = rbmod::softClip(wet * 1.012f);

        const float lvl = std::pow(rbmod::clamp01(level), 1.45f);
        const float y = 0.86f * in + wet * (0.03f + 0.84f * lvl);
        return rbmod::softClip(y * 0.95f) * 0.98f;
    }
};

class DigitalChorusPlugin : public Plugin
{
    DigitalChorusCore left;
    DigitalChorusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setLevel(params[kLevel]);
        right.setLevel(params[kLevel]);
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setLoFilter(params[kLoFilter]);
        right.setLoFilter(params[kLoFilter]);
        left.setHiFilter(params[kHiFilter]);
        right.setHiFilter(params[kHiFilter]);
    }

public:
    DigitalChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDigitalChorusDef[i];
        left.setSeed(0x43483531u);
        right.setSeed(0x43483532u);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.50f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DigitalChorus"; }
    const char* getDescription() const override { return "CE-5 style digital chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'g', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDigitalChorusNames[index];
        parameter.symbol = kDigitalChorusSymbols[index];
        parameter.ranges.min = kDigitalChorusMin[index];
        parameter.ranges.max = kDigitalChorusMax[index];
        parameter.ranges.def = kDigitalChorusDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = rbmod::clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inL[i], inR[i]);
            outL[i] = left.process(feed.left);
            outR[i] = right.process(feed.right);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalChorusPlugin)
};

Plugin* createPlugin()
{
    return new DigitalChorusPlugin();
}

END_NAMESPACE_DISTRHO
