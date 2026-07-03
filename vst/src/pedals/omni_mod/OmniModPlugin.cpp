/*
 * OmniMod - Shin-ei/Uni-Vibe style photocell phase modulation for Pedal_OmniMod.
 *
 * Local references: pedals/omnimod_1.gif and pedals/omnimod_2.jpg. This version
 * uses the same physical lamp/LDR and RC all-pass model as the Deja/Uni-Vibe
 * chorus work, but with a slightly older Shin-ei voicing and stronger throb.
 */
#include "DistrhoPlugin.hpp"
#include "OmniModParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class OmniModCore
{
    float sampleRate = 48000.0f;
    float speed = kOmniModDef[kRate];
    float intensity = kOmniModDef[kIntensity];
    float volume = kOmniModDef[kVolume];
    float mode = kOmniModDef[kMode];

    float phase = 0.0f;
    float dc = 0.0f;
    float preBias = 0.0f;
    float feedback = 0.0f;
    float throbMemory = 0.0f;

    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;
    rbmod::HighPass outputHp;
    rbmod::LampLdrModel lamp;
    rbmod::FirstOrderAllPass stages[4];

    float currentRateHz() const
    {
        const float s = std::pow(rbmod::clamp01(speed), 1.95f);
        return 0.060f + 6.45f * s;
    }

    void configureStages()
    {
        stages[0].setCap(15.0e-9f);
        stages[1].setCap(220.0e-9f);
        stages[2].setCap(470.0e-12f);
        stages[3].setCap(4.7e-9f);
        for (int i = 0; i < 4; ++i)
            stages[i].setSampleRate(sampleRate);
    }

    void updateFilters()
    {
        inputHp.setHz(30.0f, sampleRate);
        inputLp.setHz(7800.0f, sampleRate);
        outputLp.setHz(6900.0f, sampleRate);
        outputHp.setHz(20.0f, sampleRate);
    }

public:
    void reset()
    {
        inputHp.reset();
        inputLp.reset();
        outputLp.reset();
        outputHp.reset();
        lamp.reset();
        for (int i = 0; i < 4; ++i)
            stages[i].reset();
        phase = 0.0f;
        dc = 0.0f;
        preBias = 0.0f;
        feedback = 0.0f;
        throbMemory = 0.0f;
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        lamp.setSampleRate(sampleRate);
        configureStages();
        updateFilters();
        reset();
    }

    void setRate(float v) { speed = rbmod::clamp01(v); }
    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setVolume(float v) { volume = rbmod::clamp01(v); }
    void setMode(float v) { mode = v >= 0.5f ? 1.0f : 0.0f; }

    float process(float in)
    {
        phase += currentRateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float sine = std::sin(rbmod::kTwoPi * phase);
        const float lfo = rbmod::clamp01(0.50f + 0.43f * sine + 0.07f * std::sin(rbmod::kTwoPi * (phase * 2.0f + 0.11f)));
        const float inten = std::pow(rbmod::clamp01(intensity), 1.12f);
        const float light = lamp.processLight(rbmod::clamp01(0.08f + (0.20f + 0.84f * inten) * lfo));

        float x = inputHp.process(in);
        x = inputLp.process(x);
        preBias += 0.00042f * (x - preBias);
        x -= preBias;
        x = rbmod::softClip(x * 1.19f) * 0.92f;

        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);
        const float spread[4] = { 1.05f, 0.78f, 1.28f, 0.92f };
        float wet = x + feedback * (0.12f + 0.30f * inten);
        for (int i = 0; i < 4; ++i)
        {
            const float stageR = 4700.0f + ldrR * spread[i];
            wet = stages[i].process(wet, stageR);
            wet = rbmod::softClip(wet * 1.04f);
        }
        feedback = rbmod::softClip(wet) * (0.16f + 0.30f * inten);

        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        throbMemory += rbmod::onePoleCoeffHz(8.0f, sampleRate) * ((light * 2.0f - 1.0f) - throbMemory);
        wet *= 1.0f - (0.06f + 0.15f * inten) * throbMemory;

        const float vibratoMode = mode >= 0.5f ? 1.0f : 0.0f;
        const float dry = x * (0.76f * (1.0f - vibratoMode));
        const float wetLevel = vibratoMode > 0.5f ? 1.00f : 0.90f;
        float y = vibratoMode > 0.5f ? wet * wetLevel
                                      : dry - wet * wetLevel;

        const float outGain = 0.18f + 1.68f * rbmod::audioTaper(volume);
        y = outputHp.process(y);
        return rbmod::softClip(y * outGain) * 0.74f;
    }
};

class OmniModPlugin : public Plugin
{
    OmniModCore left;
    OmniModCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setIntensity(params[kIntensity]);
        right.setIntensity(params[kIntensity]);
        left.setVolume(params[kVolume]);
        right.setVolume(params[kVolume]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
    }

public:
    OmniModPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOmniModDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "OmniMod"; }
    const char* getDescription() const override { return "Uni-Vibe style photocell phase modulation"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'm', 'M', 'd'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kOmniModNames[index];
        parameter.symbol = kOmniModSymbols[index];
        parameter.ranges.min = kOmniModMin[index];
        parameter.ranges.max = kOmniModMax[index];
        parameter.ranges.def = kOmniModDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = index == (uint32_t)kMode
            ? (value >= 0.5f ? 1.0f : 0.0f)
            : rbmod::clamp01(value);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OmniModPlugin)
};

Plugin* createPlugin()
{
    return new OmniModPlugin();
}

END_NAMESPACE_DISTRHO
