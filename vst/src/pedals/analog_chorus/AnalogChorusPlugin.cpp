/*
 * 134 Stereo Chorus - MXR-style analogue stereo chorus.
 *
 * Reference: pedals/analog chorus.jpg. One MN3009/CD4013 BBD path feeds the
 * two physical outputs through a dry/effect matrix. Bass, Treble, Intensity,
 * Width and Speed retain the real panel functions.
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
    float bassNow = bass;
    float trebleNow = treble;
    float intensityNow = intensity;
    float widthNow = width;
    float speedNow = speed;
    float smoothA = 1.0f;
    float lfoPhase = 0.0f;

    rbmod::DelayBuffer bbd;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass wetLp1;
    rbmod::LowPass wetLp2;
    rbmod::LowPass bassLp;
    rbmod::LowPass trebleLp;
    rbmod::NoiseSource noise;

    float currentRateHz() const
    {
        return 0.14f * std::pow(32.0f, std::pow(rbmod::clamp01(speedNow), 0.95f));
    }

    void updateFilters()
    {
        inputHp.setHz(26.0f, sampleRate);
        inputLp.setHz(7200.0f, sampleRate);
        wetLp1.setHz(5000.0f, sampleRate);
        wetLp2.setHz(3600.0f, sampleRate);
        bassLp.setHz(180.0f, sampleRate);
        trebleLp.setHz(2600.0f, sampleRate);
    }

    float toneWet(float x)
    {
        const float low = bassLp.process(x);
        const float high = x - trebleLp.process(x);
        const float bassGain = 0.72f + 0.66f * bassNow;
        const float trebleGain = 0.62f + 0.78f * trebleNow;
        return x + (bassGain - 1.0f) * low + (trebleGain - 1.0f) * high;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.resizeForMs(sampleRate, 28.0f);
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
        wetLp1.reset();
        wetLp2.reset();
        bassLp.reset();
        trebleLp.reset();
        lfoPhase = 0.0f;
        bassNow = bass;
        trebleNow = treble;
        intensityNow = intensity;
        widthNow = width;
        speedNow = speed;
    }

    void setBass(float v) { bass = rbmod::clamp01(v); }
    void setTreble(float v) { treble = rbmod::clamp01(v); }
    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setWidth(float v) { width = rbmod::clamp01(v); }
    void setSpeed(float v) { speed = rbmod::clamp01(v); }

    void process(float in, float& outA, float& outB)
    {
        bassNow += smoothA * (bass - bassNow);
        trebleNow += smoothA * (treble - trebleNow);
        intensityNow += smoothA * (intensity - intensityNow);
        widthNow += smoothA * (width - widthNow);
        speedNow += smoothA * (speed - speedNow);

        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float sine = std::sin(rbmod::kTwoPi * lfoPhase);
        const float tri = 1.0f - 4.0f * std::fabs(lfoPhase - 0.5f);
        const float lfo = 0.72f * sine + 0.28f * tri;
        const float w = std::pow(rbmod::clamp01(widthNow), 1.45f);
        const float delayMs = rbmod::clamp(12.4f + (0.12f + 7.05f * w) * lfo,
                                           4.5f, 20.5f);

        float dry = inputHp.process(in);
        dry = rbmod::softClip(dry * 1.025f) / 1.025f;
        const float bbdIn = inputLp.process(dry);
        float wet = bbd.readCubic(delayMs * 0.001f * sampleRate);
        bbd.write(bbdIn);
        wet = wetLp2.process(wetLp1.process(wet));
        wet += 0.0045f * (wet * wet * wet - wet);
        wet += noise.next() * (0.000006f + 0.000014f * w);
        wet = toneWet(wet);

        const float i = std::pow(rbmod::clamp01(intensityNow), 1.20f);
        const float side = 1.35f * i;
        const float matrixGain = 0.94f / std::sqrt(1.0f + side * side);
        outA = matrixGain * (dry + side * wet);
        outB = matrixGain * (dry - side * wet);
    }
};

class AnalogChorusPlugin : public Plugin
{
    AnalogChorusCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setBass(params[kBass]);
        core.setTreble(params[kTreble]);
        core.setIntensity(params[kIntensity]);
        core.setWidth(params[kWidth]);
        core.setSpeed(params[kSpeed]);
    }

public:
    AnalogChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAnalogChorusDef[i];
        core.setSeed(0x41313334u);
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AnalogChorus"; }
    const char* getDescription() const override { return "MN3009-style stereo analog chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogChorusPlugin)
};

Plugin* createPlugin()
{
    return new AnalogChorusPlugin();
}

END_NAMESPACE_DISTRHO
