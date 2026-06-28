/*
 * 134 Stereo Chorus - MXR-style analog stereo chorus.
 *
 * Reference: pedals/analog chorus.jpg. The model follows the MN3009/CD4013
 * clocked BBD path, 4558/1458 filtering, CA3080-style control behaviour and
 * the real panel controls: Bass, Treble, Intensity, Width and Speed.
 */
#include "DistrhoPlugin.hpp"
#include "AnalogChorusParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class AnalogChorusCore
{
    float sampleRate = 48000.0f;
    float bass = kAnalogChorusDef[kBass];
    float treble = kAnalogChorusDef[kTreble];
    float intensity = kAnalogChorusDef[kIntensity];
    float width = kAnalogChorusDef[kWidth];
    float speed = kAnalogChorusDef[kSpeed];
    float phaseOffset = 0.0f;
    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    rbmod::DelayBuffer bbd;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass wetLp1;
    rbmod::LowPass wetLp2;
    rbmod::LowPass bassLp;
    rbmod::LowPass trebleLp;
    rbmod::BbdCompander compander;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.045f + 4.60f * std::pow(rbmod::clamp01(speed), 2.05f);
    }

    void updateFilters()
    {
        const float w = std::pow(rbmod::clamp01(width), 1.75f);
        inputHp.setHz(26.0f, sampleRate);
        inputLp.setHz(6900.0f - 900.0f * w, sampleRate);
        wetLp1.setHz(4600.0f - 1150.0f * w, sampleRate);
        wetLp2.setHz(3300.0f - 550.0f * w, sampleRate);
        bassLp.setHz(180.0f, sampleRate);
        trebleLp.setHz(2600.0f, sampleRate);
    }

    float tone(float x)
    {
        const float low = bassLp.process(x);
        const float high = x - trebleLp.process(x);
        const float bassGain = 0.70f + 0.72f * bass;
        const float trebleGain = 0.58f + 0.88f * treble;
        return x + (bassGain - 1.0f) * low + (trebleGain - 1.0f) * high;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.resizeForMs(sampleRate, 58.0f);
        compander.setSampleRate(sampleRate, 28.0f);
        updateFilters();
        reset();
    }

    void setSeed(unsigned int seed) { noise.seed(seed); }
    void setPhaseOffset(float v) { phaseOffset = v - std::floor(v); }

    void reset()
    {
        bbd.reset();
        inputHp.reset();
        inputLp.reset();
        wetLp1.reset();
        wetLp2.reset();
        bassLp.reset();
        trebleLp.reset();
        compander.reset();
        lfoPhase = phaseOffset;
        feedback = 0.0f;
    }

    void setBass(float v) { bass = rbmod::clamp01(v); }
    void setTreble(float v) { treble = rbmod::clamp01(v); }
    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setWidth(float v) { width = rbmod::clamp01(v); updateFilters(); }
    void setSpeed(float v) { speed = rbmod::clamp01(v); }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(rbmod::kTwoPi * phase);
        const float slow = std::sin(rbmod::kTwoPi * (phase * 0.5f + 0.18f));
        const float wobble = 0.82f * sine + 0.18f * slow;

        const float w = std::pow(rbmod::clamp01(width), 1.75f);
        const float baseMs = 12.0f + 3.8f * (1.0f - w);
        const float widthMs = 0.10f + 8.55f * w;
        float delayMs = baseMs + widthMs * wobble;
        delayMs = rbmod::clamp(delayMs, 3.2f, 46.0f);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = rbmod::softClip(x * 1.05f);

        const float fb = 0.012f + 0.035f * intensity;
        const float write = rbmod::softClip(x + feedback * fb);
        float wet = bbd.read(delayMs * 0.001f * sampleRate);
        bbd.write(write);

        wet += noise.next() * (0.00022f + 0.0010f * rbmod::smoothstep(width));
        wet = wetLp2.process(wetLp1.process(wet));
        wet = compander.process(wet, 0.45f + 0.35f * width);
        feedback = wet;

        const float i = std::pow(rbmod::clamp01(intensity), 1.55f);
        const float mixed = 0.82f * in + wet * (0.12f + 0.74f * i);
        return rbmod::softClip(tone(mixed) * 0.92f) * 0.97f;
    }
};

class AnalogChorusPlugin : public Plugin
{
    AnalogChorusCore left;
    AnalogChorusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setBass(params[kBass]);
        right.setBass(params[kBass]);
        left.setTreble(params[kTreble]);
        right.setTreble(params[kTreble]);
        left.setIntensity(params[kIntensity]);
        right.setIntensity(params[kIntensity]);
        left.setWidth(params[kWidth]);
        right.setWidth(params[kWidth]);
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
    }

public:
    AnalogChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAnalogChorusDef[i];
        left.setSeed(0x41313334u);
        right.setSeed(0x41313335u);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.47f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AnalogChorus"; }
    const char* getDescription() const override { return "MN3009-style stereo analog chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'n', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAnalogChorusNames[index];
        parameter.symbol = kAnalogChorusSymbols[index];
        parameter.ranges.min = kAnalogChorusMin[index];
        parameter.ranges.max = kAnalogChorusMax[index];
        parameter.ranges.def = kAnalogChorusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogChorusPlugin)
};

Plugin* createPlugin()
{
    return new AnalogChorusPlugin();
}

END_NAMESPACE_DISTRHO
