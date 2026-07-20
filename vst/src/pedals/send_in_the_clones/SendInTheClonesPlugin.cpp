/*
 * Attack of the Clones - EHX Clone Theory style BBD chorus/vibrato.
 *
 * Reference: pedals/send in the clones.png. The layout identifies the core
 * parts: RC4558P, MN3007, CD4047BE, LM1458, 2N4302/2N5458, ICL7660S, 78L15
 * and controls Ch/Vib, Depth, Rate plus a Vib/Flange switch.
 */
#include "DistrhoPlugin.hpp"
#include "SendInTheClonesParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class CloneTheoryCore
{
    float sampleRate = 48000.0f;
    float rate = kSitcDef[kRate];
    float depth = kSitcDef[kDepth];
    float chVib = kSitcDef[kChVib];
    float flange = kSitcDef[kFlange];
    float rateNow = rate;
    float depthNow = depth;
    float chVibNow = chVib;
    float smoothA = 1.0f;
    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    rbmod::DelayBuffer bbd;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass bbdLp1;
    rbmod::LowPass bbdLp2;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.065f + 5.85f * std::pow(rbmod::clamp01(rateNow), 2.00f);
    }

    void updateFilters()
    {
        const float f = flange >= 0.5f ? 1.0f : 0.0f;
        inputHp.setHz(30.0f, sampleRate);
        inputLp.setHz(7600.0f, sampleRate);
        bbdLp1.setHz(5200.0f - 400.0f * f, sampleRate);
        bbdLp2.setHz(4100.0f - 350.0f * f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.resizeForMs(sampleRate, 55.0f);
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed) { noise.seed(seed); }

    void reset()
    {
        bbd.reset();
        inputHp.reset();
        inputLp.reset();
        bbdLp1.reset();
        bbdLp2.reset();
        lfoPhase = 0.0f;
        feedback = 0.0f;
        rateNow = rate;
        depthNow = depth;
        chVibNow = chVib;
    }

    void setRate(float v) { rate = rbmod::clamp01(v); }
    void setDepth(float v) { depth = rbmod::clamp01(v); updateFilters(); }
    void setChVib(float v) { chVib = rbmod::clamp01(v); }
    void setFlange(float v) { flange = v >= 0.5f ? 1.0f : 0.0f; updateFilters(); }

    float process(float in)
    {
        rateNow += smoothA * (rate - rateNow);
        depthNow += smoothA * (depth - depthNow);
        chVibNow += smoothA * (chVib - chVibNow);

        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float p = lfoPhase;
        const float sine = std::sin(rbmod::kTwoPi * p);
        const float second = std::sin(rbmod::kTwoPi * (2.0f * p + 0.22f));
        const float wobble = 0.86f * sine + 0.14f * second;

        const float d = std::pow(rbmod::clamp01(depthNow), 1.55f);
        const float f = flange >= 0.5f ? 1.0f : 0.0f;
        const float baseMs = f > 0.5f ? 3.4f : 12.2f;
        const float widthMs = (f > 0.5f ? 2.0f : 7.8f) * d + 0.08f;
        float delayMs = baseMs + widthMs * wobble;
        delayMs = rbmod::clamp(delayMs, f > 0.5f ? 0.95f : 3.0f, f > 0.5f ? 9.0f : 42.0f);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = rbmod::softClip(x * (1.05f + 0.06f * d));

        const float fb = f > 0.5f ? -0.28f : 0.022f;
        const float write = rbmod::softClip(x + feedback * fb);
        float wet = bbd.readCubic(delayMs * 0.001f * sampleRate);
        bbd.write(write);

        wet += noise.next() * (0.000012f + 0.000035f * d);
        wet = bbdLp2.process(bbdLp1.process(wet));
        wet = rbmod::softClip(wet * 1.018f) / 1.018f;
        feedback = wet;

        const float vib = std::pow(rbmod::clamp01(chVibNow), 1.20f);
        const float dryLevel = 1.0f - 0.92f * vib;
        const float wetLevel = 0.42f + 0.58f * vib;
        const float y = dryLevel * in + wetLevel * wet;
        return rbmod::softClip(y * 0.98f);
    }
};

class SendInTheClonesPlugin : public Plugin
{
    CloneTheoryCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setRate(params[kRate]);
        core.setDepth(params[kDepth]);
        core.setChVib(params[kChVib]);
        core.setFlange(params[kFlange]);
    }

public:
    SendInTheClonesPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSitcDef[i];
        core.setSeed(0x53434c31u);
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SendInTheClones"; }
    const char* getDescription() const override { return "Clone Theory style BBD chorus/vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'C', 'l', 'n'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kFlange)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kSitcNames[index];
        parameter.symbol = kSitcSymbols[index];
        parameter.ranges.min = kSitcMin[index];
        parameter.ranges.max = kSitcMax[index];
        parameter.ranges.def = kSitcDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kFlange) ? (value >= 0.5f ? 1.0f : 0.0f)
                                                     : rbmod::clamp01(value);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SendInTheClonesPlugin)
};

Plugin* createPlugin()
{
    return new SendInTheClonesPlugin();
}

END_NAMESPACE_DISTRHO
