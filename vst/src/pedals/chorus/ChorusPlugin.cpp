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
    float rateNow = rate;
    float depthNow = depth;
    float smoothA = 1.0f;
    float lfoPhase = 0.0f;

    rbmod::DelayBuffer bbd;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass bbdLp1;
    rbmod::LowPass bbdLp2;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        // VR1 is reverse-log: noon is around 1 Hz, not the midpoint of the
        // electrical range. Keep the measured CE family range conservative.
        return 0.25f * std::pow(18.0f, std::pow(rbmod::clamp01(rateNow), 1.28f));
    }

    void updateFilters()
    {
        // R/C networks around IC1/Q2-Q4 are fixed. Depth moves the MN3101
        // clock and must not retune the analogue filters.
        inputHp.setHz(32.0f, sampleRate);
        inputLp.setHz(7900.0f, sampleRate);
        bbdLp1.setHz(5600.0f, sampleRate);
        bbdLp2.setHz(4100.0f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.resizeForMs(sampleRate, 24.0f);
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
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
        lfoPhase = 0.0f;
        rateNow = rate;
        depthNow = depth;
    }

    void setRate(float v)
    {
        rate = rbmod::clamp01(v);
    }

    void setDepth(float v)
    {
        depth = rbmod::clamp01(v);
    }

    float process(float in)
    {
        rateNow += smoothA * (rate - rateNow);
        depthNow += smoothA * (depth - depthNow);
        const float d = std::pow(rbmod::clamp01(depthNow), 1.45f);
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        // TL022 oscillator followed by the MN3101 control network: a rounded
        // triangle, continuous at the phase wrap.
        const float tri = 1.0f - 4.0f * std::fabs(lfoPhase - 0.5f);
        const float sine = std::sin(rbmod::kTwoPi * lfoPhase);
        const float lfo = 0.38f * sine + 0.62f * tri;

        // MN3007 has 1024 stages and MN3101 provides a two-phase clock.
        const float clockCentre = 50000.0f;
        const float clockSpan = 0.015f + 0.52f * d;
        const float clockHz = rbmod::clamp(clockCentre * (1.0f + clockSpan * lfo),
                                            28000.0f, 100000.0f);
        const float delayMs = 1000.0f * 1024.0f / (2.0f * clockHz);

        float dry = inputHp.process(in);
        dry = rbmod::softClip(dry * 1.025f) / 1.025f;
        float x = inputLp.process(dry);

        float wet = bbd.readCubic(delayMs * 0.001f * sampleRate);
        bbd.write(x);

        wet += noise.next() * (0.000008f + 0.000018f * d);
        wet = bbdLp2.process(bbdLp1.process(wet));
        wet += 0.004f * (wet * wet * wet - wet);

        // R23/R24 are equal 47 k mixer branches. The wet coefficient represents
        // MN3007 insertion loss, not a user-facing Mix control.
        const float y = 0.76f * (dry + 0.72f * wet);
        return rbmod::softClip(y * 1.015f) / 1.015f;
    }
};

class ChorusPlugin : public Plugin
{
    ChorusCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setRate(params[kRate]);
        core.setDepth(params[kDepth]);
    }

public:
    ChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChorusDef[i];
        core.setSeed(0x43483231u);
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Chorus"; }
    const char* getDescription() const override { return "Boss CE-2 style MN3007 chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
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
            const float output = core.process(mono);
            outL[i] = output;
            outR[i] = output;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusPlugin)
};

Plugin* createPlugin()
{
    return new ChorusPlugin();
}

END_NAMESPACE_DISTRHO
