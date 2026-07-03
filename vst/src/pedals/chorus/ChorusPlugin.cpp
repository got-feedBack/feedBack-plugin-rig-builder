/*
 * CH-2 - Boss CE-2 style chorus.
 *
 * Reference: pedals/chorus.pdf. Component-guided model of the CE-2 signal
 * blocks: uPC4558 input/output buffering, TL022 LFO, MN3101 clock, MN3007
 * BBD delay, 1S2473/1S1588 bias protection and the fixed dry/wet mix of the
 * real two-knob pedal.
 */
#include "DistrhoPlugin.hpp"
#include "ChorusParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class ChorusCore
{
    float sampleRate = 48000.0f;
    float rate = kChorusDef[kRate];
    float depth = kChorusDef[kDepth];
    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    rbmod::DelayBuffer bbd;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass bbdLp1;
    rbmod::LowPass bbdLp2;
    rbmod::BbdCompander compander;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.065f + 5.35f * std::pow(rbmod::clamp01(rate), 2.05f);
    }

    void updateFilters()
    {
        const float d = std::pow(rbmod::clamp01(depth), 1.85f);
        inputHp.setHz(32.0f, sampleRate);
        inputLp.setHz(7800.0f - 1300.0f * d, sampleRate);
        bbdLp1.setHz(5200.0f - 1450.0f * d, sampleRate);
        bbdLp2.setHz(3600.0f - 650.0f * d, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.resizeForMs(sampleRate, 36.0f);
        compander.setSampleRate(sampleRate, 24.0f);
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed)
    {
        noise.seed(seed);
    }

    void reset()
    {
        bbd.reset();
        inputHp.reset();
        inputLp.reset();
        bbdLp1.reset();
        bbdLp2.reset();
        compander.reset();
        lfoPhase = 0.0f;
        feedback = 0.0f;
    }

    void setRate(float v)
    {
        rate = rbmod::clamp01(v);
    }

    void setDepth(float v)
    {
        depth = rbmod::clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float d = std::pow(rbmod::clamp01(depth), 1.85f);
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        // TL022 LFO is rounded-triangle-ish once it hits the MN3101 clock.
        const float tri = 4.0f * std::fabs(lfoPhase - 0.5f) - 1.0f;
        const float sine = std::sin(rbmod::kTwoPi * lfoPhase);
        const float lfo = 0.58f * sine - 0.42f * tri;

        const float baseMs = 8.9f + 1.20f * (1.0f - d);
        const float widthMs = 0.08f + 4.35f * d;
        float delayMs = baseMs + widthMs * lfo;
        delayMs = rbmod::clamp(delayMs, 3.4f, 20.0f);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = rbmod::softClip(x * (1.045f + 0.075f * d));

        const float write = rbmod::softClip(x + 0.018f * feedback);
        float wet = bbd.read(delayMs * 0.001f * sampleRate);
        bbd.write(write);

        const float clockPenalty = rbmod::clamp01((delayMs - 4.0f) / 18.0f);
        wet += noise.next() * (0.00028f + 0.00125f * clockPenalty);
        wet = bbdLp2.process(bbdLp1.process(wet));
        wet = compander.process(wet, 0.42f + 0.45f * d);
        feedback = wet;

        // CE-2 fixed mixer: dry dominates; wet is dark and below unity.
        const float y = 0.78f * in + 0.52f * wet;
        return rbmod::softClip(y * 0.96f) * 0.98f;
    }
};

class ChorusPlugin : public Plugin
{
    ChorusCore left;
    ChorusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    ChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChorusDef[i];
        left.setSeed(0x43483231u);
        right.setSeed(0x43483232u);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Chorus"; }
    const char* getDescription() const override { return "Boss CE-2 style MN3007 chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'h', 'o', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kChorusNames[index];
        parameter.symbol = kChorusSymbols[index];
        parameter.ranges.min = kChorusMin[index];
        parameter.ranges.max = kChorusMax[index];
        parameter.ranges.def = kChorusDef[index];
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
            outL[i] = left.process(inL[i]);
            outR[i] = right.process(inR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusPlugin)
};

Plugin* createPlugin()
{
    return new ChorusPlugin();
}

END_NAMESPACE_DISTRHO
