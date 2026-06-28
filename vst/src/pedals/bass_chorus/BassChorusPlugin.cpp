/*
 * BassChorus - Boss CEB-3 Bass Chorus style model for Bass_Pedal_BassChorus.
 *
 * Reference: pedals/BossCEB-3.jpg plus local ES56028/NJM022B/M5223/2SK879Y
 * datasheets. This pedal is a short digital delay chorus, not a MN3007 BBD:
 * the ES56028 runs in its short/surround delay range, while the analog side
 * provides JFET buffering, pre/de-emphasis, LFO control and the bass Low Filter
 * that keeps fundamentals mostly dry.
 */
#include "DistrhoPlugin.hpp"
#include "BassChorusParams.h"
#include "../_shared/ChorusComponents.h"
#include "../_shared/opamp.hpp"
#include "../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float jfetBuffer2SK880(float x)
{
    // Source follower: almost unity gain, soft headroom before the NJM022B.
    return rbmod::softClip(x * 0.985f) * 0.992f;
}

static inline float jfetSwitch2SK879(float x, float control)
{
    const float gate = 0.965f + 0.035f * rbmod::smoothstep(control);
    return rbmod::softClip(x * gate * 1.015f) * 0.990f;
}

static inline float quantizeEs56028(float x)
{
    x = rbmod::clamp(x, -0.42f, 0.42f);
    const float q = 4096.0f;
    return std::floor(x * q + 0.5f) / q;
}

} // namespace

class Es56028ShortDelay
{
    float sampleRate = 48000.0f;
    rbmod::DelayBuffer memory;
    rbmod::LowPass lpf1;
    rbmod::LowPass lpf2;
    rbmod::LowPass dacHold;
    rbmod::BbdCompander compander;
    rbmod::NoiseSource noise;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        memory.resizeForMs(sampleRate, 46.0f);
        compander.setSampleRate(sampleRate, 18.0f);
        lpf1.setHz(11800.0f, sampleRate);
        lpf2.setHz(9300.0f, sampleRate);
        dacHold.setHz(15500.0f, sampleRate);
        reset();
    }

    void setSeed(unsigned int seed)
    {
        noise.seed(seed);
    }

    void reset()
    {
        memory.reset();
        lpf1.reset();
        lpf2.reset();
        dacHold.reset();
        compander.reset();
    }

    float process(float input, float delayMs, float depthAmount)
    {
        delayMs = rbmod::clamp(delayMs, 4.1f, 41.0f);

        float x = lpf1.process(input);
        x = compander.process(x, 0.28f + 0.20f * depthAmount);
        x = rbmod::softClip(x * 1.018f);

        const float delayed = memory.read(delayMs * 0.001f * sampleRate);
        memory.write(x);

        float y = lpf2.process(delayed);
        y = quantizeEs56028(y);
        y += noise.next() * (0.000018f + 0.000030f * depthAmount);
        y = dacHold.process(y);
        return rbmod::softClip(y * 1.012f);
    }
};

class Ceb3Channel
{
    float sampleRate = 48000.0f;
    float rate = kBassChorusDef[kRate];
    float depth = kBassChorusDef[kDepth];
    float lowFilter = kBassChorusDef[kLowFilter];
    float effectLevel = kBassChorusDef[kELevel];
    float phaseOffset = 0.0f;
    float lfoPhase = 0.0f;

    rbshared::OpAmpStage inputAmp;
    rbshared::OpAmpStage lfoAmp;
    rbshared::OpAmpStage mixAmp;
    rbcomponents::AntiParallelDiodePair switchingDiodes;
    Es56028ShortDelay es56028;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass preEmphasis;
    rbmod::HighPass wetHp;
    rbmod::LowPass wetLp;
    rbmod::LowPass outputLp;
    rbmod::LowPass dryLowSense;

    float rateHz() const
    {
        const float cTaper = std::pow(rbmod::clamp01(rate), 2.05f);
        return 0.075f + 6.15f * std::pow(cTaper, 1.32f);
    }

    void updateFilters()
    {
        const float lf = rbmod::smoothstep(lowFilter);

        inputHp.setHz(18.0f, sampleRate);
        inputLp.setHz(13500.0f, sampleRate);
        preEmphasis.setHz(2450.0f, sampleRate);

        // CEB-3 Low Filter: clockwise removes more low end from the wet delay.
        wetHp.setHz(45.0f + 430.0f * lf + 360.0f * lf * lf, sampleRate);
        wetLp.setHz(9000.0f - 1700.0f * lf, sampleRate);
        dryLowSense.setHz(90.0f + 520.0f * lf, sampleRate);
        outputLp.setHz(16800.0f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        inputAmp.setSpec(rbshared::njm022bSpec());
        lfoAmp.setSpec(rbshared::m5223Spec());
        mixAmp.setSpec(rbshared::njm022bSpec());
        inputAmp.setSampleRate(sampleRate);
        lfoAmp.setSampleRate(sampleRate);
        mixAmp.setSampleRate(sampleRate);
        switchingDiodes.setSpec(rbcomponents::diode1SS355());
        switchingDiodes.setSourceR(180000.0f);
        es56028.setSampleRate(sampleRate);
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed)
    {
        es56028.setSeed(seed);
    }

    void setPhaseOffset(float v)
    {
        phaseOffset = v - std::floor(v);
        lfoPhase = phaseOffset;
    }

    void reset()
    {
        inputAmp.reset();
        lfoAmp.reset();
        mixAmp.reset();
        switchingDiodes.reset();
        es56028.reset();
        inputHp.reset();
        inputLp.reset();
        preEmphasis.reset();
        wetHp.reset();
        wetLp.reset();
        outputLp.reset();
        dryLowSense.reset();
        lfoPhase = phaseOffset;
    }

    void setParams(float newRate, float newDepth, float newLowFilter, float newEffectLevel)
    {
        rate = rbmod::clamp01(newRate);
        depth = rbmod::clamp01(newDepth);
        lowFilter = rbmod::clamp01(newLowFilter);
        effectLevel = rbmod::clamp01(newEffectLevel);
        updateFilters();
    }

    float process(float input)
    {
        lfoPhase += rateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float p = lfoPhase + phaseOffset;
        const float frac = p - std::floor(p);
        const float tri = 4.0f * std::fabs(frac - 0.5f) - 1.0f;
        const float sine = std::sin(rbmod::kTwoPi * frac);
        const float lfoRaw = 0.64f * tri + 0.36f * sine;
        const float lfo = lfoAmp.process(lfoRaw, 1.0f);

        const float depthLin = std::pow(rbmod::clamp01(depth), 1.75f);
        const float depthMs = 0.12f + 6.60f * depthLin;
        const float baseMs = 13.2f + 2.8f * (1.0f - depthLin);
        const float delayMs = rbmod::clamp(baseMs + depthMs * lfo, 4.1f, 32.5f);

        float dry = jfetBuffer2SK880(input);
        dry = inputHp.process(dry);
        dry = inputLp.process(dry);

        float x = inputAmp.process(dry * 1.08f, 2.2f);
        const float emphasis = preEmphasis.process(x);
        x = rbmod::softClip((0.91f * x + 0.13f * (x - emphasis)) * 1.02f);

        float wet = es56028.process(x, delayMs, depthLin);
        wet = wetHp.process(wet);
        wet = wetLp.process(wet);
        wet = jfetSwitch2SK879(wet, effectLevel);

        const float switchLeak = switchingDiodes.process(wet * 0.18f) * 0.018f;
        wet += switchLeak;

        const float lowSense = dryLowSense.process(dry);
        const float lf = rbmod::smoothstep(lowFilter);
        const float level = std::pow(rbmod::clamp01(effectLevel), 1.45f);
        const float wetGain = (0.01f + 0.86f * level) * (0.96f - 0.12f * lf);
        const float dryGain = 0.92f - 0.06f * level;

        float mixed = dry * dryGain + wet * wetGain + lowSense * (0.018f * lf);
        mixed = mixAmp.process(mixed, 1.15f);
        mixed = outputLp.process(mixed);
        return rbmod::softClip(mixed * 0.985f);
    }
};

class BassChorusPlugin : public Plugin
{
    Ceb3Channel left;
    Ceb3Channel right;
    float params[kParamCount];

    void applyParams()
    {
        left.setParams(params[kRate], params[kDepth], params[kLowFilter], params[kELevel]);
        right.setParams(params[kRate], params[kDepth], params[kLowFilter], params[kELevel]);
    }

public:
    BassChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassChorusDef[i];

        left.setSeed(0x43454231u);
        right.setSeed(0x43454232u);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.33f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyParams();
    }

protected:
    const char* getLabel() const override { return "BassChorus"; }
    const char* getDescription() const override { return "CEB-3 style ES56028 bass chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;

        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassChorusNames[index];
        parameter.symbol = kBassChorusSymbols[index];
        parameter.ranges.min = kBassChorusMin[index];
        parameter.ranges.max = kBassChorusMax[index];
        parameter.ranges.def = kBassChorusDef[index];
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
        applyParams();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyParams();
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassChorusPlugin)
};

Plugin* createPlugin()
{
    return new BassChorusPlugin();
}

END_NAMESPACE_DISTRHO
