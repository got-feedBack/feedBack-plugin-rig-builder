/*
 * CH-5 - Boss CE-5 style digital chorus.
 *
 * Reference: pedals/digital chorus.png. The ES56028S provides one modulated
 * delay path. Output A is the mono dry/effect mix and Output B is the direct
 * path; they are not two independent chorus engines.
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
    float levelNow = level;
    float rateNow = rate;
    float depthNow = depth;
    float loNow = loFilter;
    float hiNow = hiFilter;
    float smoothA = 1.0f;
    float lfoPhase = 0.0f;
    float pre = 0.0f;
    unsigned int filterUpdateCounter = 0;

    rbmod::DelayBuffer delay;
    rbmod::HighPass inputHp;
    rbmod::HighPass wetHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass preEmphasis;
    rbmod::LowPass wetLp;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.22f * std::pow(20.5f, std::pow(rbmod::clamp01(rateNow), 1.05f));
    }

    void updateFilters()
    {
        const float lo = rbmod::smoothstep(loNow);
        const float hi = rbmod::smoothstep(hiNow);
        inputHp.setHz(22.0f, sampleRate);
        inputLp.setHz(13200.0f, sampleRate);
        wetHp.setHz(34.0f + 780.0f * lo, sampleRate);
        wetLp.setHz(12500.0f - 8700.0f * hi, sampleRate);
        preEmphasis.setHz(2300.0f + 5200.0f * (1.0f - hi), sampleRate);
    }

    static float quantize12(float x)
    {
        x = rbmod::clamp(x, -0.95f, 0.95f);
        return std::floor(x * 2048.0f + 0.5f) / 2048.0f;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resizeForMs(sampleRate, 32.0f);
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed) { noise.seed(seed); }

    void reset()
    {
        delay.reset();
        inputHp.reset();
        wetHp.reset();
        inputLp.reset();
        preEmphasis.reset();
        wetLp.reset();
        lfoPhase = 0.0f;
        pre = 0.0f;
        levelNow = level;
        rateNow = rate;
        depthNow = depth;
        loNow = loFilter;
        hiNow = hiFilter;
        filterUpdateCounter = 0;
        updateFilters();
    }

    void setLevel(float v) { level = rbmod::clamp01(v); }
    void setRate(float v) { rate = rbmod::clamp01(v); }
    void setDepth(float v) { depth = rbmod::clamp01(v); }
    void setLoFilter(float v) { loFilter = rbmod::clamp01(v); }
    void setHiFilter(float v) { hiFilter = rbmod::clamp01(v); }

    void process(float in, float& outputA, float& outputB)
    {
        levelNow += smoothA * (level - levelNow);
        rateNow += smoothA * (rate - rateNow);
        depthNow += smoothA * (depth - depthNow);
        loNow += smoothA * (loFilter - loNow);
        hiNow += smoothA * (hiFilter - hiNow);
        if ((filterUpdateCounter++ & 15u) == 0u)
            updateFilters();

        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);
        const float tri = 1.0f - 4.0f * std::fabs(lfoPhase - 0.5f);
        const float sine = std::sin(rbmod::kTwoPi * lfoPhase);
        const float lfo = 0.62f * tri + 0.38f * sine;

        const float d = std::pow(rbmod::clamp01(depthNow), 1.55f);
        const float delayMs = rbmod::clamp(14.6f + (0.12f + 8.60f * d) * lfo,
                                           5.8f, 23.4f);

        float dry = inputHp.process(in);
        dry = rbmod::softClip(dry * 1.012f) / 1.012f;
        float x = inputLp.process(dry);
        pre = preEmphasis.process(x);
        x = 0.90f * x + 0.10f * (x - pre);

        float wet = delay.readCubic(delayMs * 0.001f * sampleRate);
        delay.write(x);
        wet = quantize12(wet);
        wet += noise.next() * (0.000004f + 0.000010f * d);
        wet = wetLp.process(wetHp.process(wet));

        // The real E.Level pot is already useful around noon. The previous
        // super-linear taper left only ~0.28 wet at the default 0.42 setting.
        const float effect = 1.65f * std::pow(rbmod::clamp01(levelNow), 0.75f);
        const float mixGain = 0.96f / std::sqrt(1.0f + 0.34f * effect * effect);
        outputA = mixGain * (dry + effect * wet);
        outputB = 0.96f * dry;
    }
};

class DigitalChorusPlugin : public Plugin
{
    DigitalChorusCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setLevel(params[kLevel]);
        core.setRate(params[kRate]);
        core.setDepth(params[kDepth]);
        core.setLoFilter(params[kLoFilter]);
        core.setHiFilter(params[kHiFilter]);
    }

public:
    DigitalChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDigitalChorusDef[i];
        core.setSeed(0x43483531u);
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DigitalChorus"; }
    const char* getDescription() const override { return "CE-5 style ES56028S digital chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 4, 0); }
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
        core.setSampleRate((float)newSampleRate);
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
            const float mono = 0.5f * (feed.left + feed.right);
            core.process(mono, outL[i], outR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalChorusPlugin)
};

Plugin* createPlugin()
{
    return new DigitalChorusPlugin();
}

END_NAMESPACE_DISTRHO
