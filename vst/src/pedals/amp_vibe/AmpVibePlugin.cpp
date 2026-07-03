/*
 * AmpVibe - Uni-Vibe style optical modulation for Pedal_AmpVibe.
 *
 * Local references: pedals/ampvibe.jpg, pedals/chorus 20/ElectroVibe-PedalPCB.pdf
 * and the Pisotones Uni-Vibe notes. The DSP follows the transistor preamp,
 * four LDR-controlled all-pass stages, incandescent lamp inertia and the
 * Chorus/Vibrato output switch. the game's Speed and Mix are mapped to the
 * real Speed and Intensity controls; Volume and Mode remain editable in the UI.
 */
#include "DistrhoPlugin.hpp"
#include "AmpVibeParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class AmpVibeCore
{
    float sampleRate = 48000.0f;
    float speed = kAmpVibeDef[kSpeed];
    float intensity = kAmpVibeDef[kIntensity];
    float volume = kAmpVibeDef[kVolume];
    float mode = kAmpVibeDef[kMode];

    float phase = 0.0f;
    float dc = 0.0f;
    float preBias = 0.0f;
    float feedback = 0.0f;

    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;
    rbmod::HighPass outputHp;
    rbmod::LampLdrModel lamp;
    rbmod::FirstOrderAllPass stages[4];

    float currentRateHz() const
    {
        const float s = std::pow(rbmod::clamp01(speed), 2.05f);
        return 0.070f + 6.80f * s;
    }

    void configureStages()
    {
        // ElectroVibe/Uni-Vibe phase network caps; the LDR resistance does the
        // actual sweep and stage mismatch gives the asymmetric swirl.
        stages[0].setCap(15.0e-9f);
        stages[1].setCap(220.0e-9f);
        stages[2].setCap(470.0e-12f);
        stages[3].setCap(4.7e-9f);
        for (int i = 0; i < 4; ++i)
            stages[i].setSampleRate(sampleRate);
    }

    void updateFilters()
    {
        inputHp.setHz(28.0f, sampleRate);
        inputLp.setHz(9000.0f, sampleRate);
        outputLp.setHz(7400.0f, sampleRate);
        outputHp.setHz(18.0f, sampleRate);
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
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        lamp.setSampleRate(sampleRate);
        configureStages();
        updateFilters();
        reset();
    }

    void setSpeed(float v) { speed = rbmod::clamp01(v); }
    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setVolume(float v) { volume = rbmod::clamp01(v); }
    void setMode(float v) { mode = v >= 0.5f ? 1.0f : 0.0f; }

    float process(float in)
    {
        phase += currentRateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float lfo = 0.5f + 0.5f * std::sin(rbmod::kTwoPi * phase);
        const float inten = std::pow(rbmod::clamp01(intensity), 1.18f);
        const float drive = rbmod::clamp01(0.10f + (0.18f + 0.82f * inten) * lfo);
        const float light = lamp.processLight(drive);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        preBias += 0.00050f * (x - preBias);
        x -= preBias;
        x = rbmod::softClip(x * 1.14f) * 0.94f;

        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);
        const float spread[4] = { 1.00f, 0.84f, 1.22f, 0.94f };
        float wet = x + feedback * (0.08f + 0.20f * inten);
        for (int i = 0; i < 4; ++i)
        {
            const float stageR = 4700.0f + ldrR * spread[i];
            wet = stages[i].process(wet, stageR);
            wet = rbmod::softClip(wet * 1.03f);
        }
        feedback = rbmod::softClip(wet) * (0.10f + 0.22f * inten);

        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        const float vibratoMode = mode >= 0.5f ? 1.0f : 0.0f;
        const float dry = x * (0.78f * (1.0f - vibratoMode));
        const float wetLevel = vibratoMode > 0.5f ? 0.98f : 0.88f;
        float y = vibratoMode > 0.5f ? wet * wetLevel
                                      : dry - wet * wetLevel;

        const float throb = 1.0f - (0.04f + 0.11f * inten) * (light * 2.0f - 1.0f);
        const float outGain = 0.18f + 1.70f * rbmod::audioTaper(volume);
        y = outputHp.process(y * throb);
        return rbmod::softClip(y * outGain) * 0.72f;
    }
};

class AmpVibePlugin : public Plugin
{
    AmpVibeCore left;
    AmpVibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setIntensity(params[kIntensity]);
        right.setIntensity(params[kIntensity]);
        left.setVolume(params[kVolume]);
        right.setVolume(params[kVolume]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
    }

public:
    AmpVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAmpVibeDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AmpVibe"; }
    const char* getDescription() const override { return "Uni-Vibe style optical modulation"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'm', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kAmpVibeNames[index];
        parameter.symbol = kAmpVibeSymbols[index];
        parameter.ranges.min = kAmpVibeMin[index];
        parameter.ranges.max = kAmpVibeMax[index];
        parameter.ranges.def = kAmpVibeDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpVibePlugin)
};

Plugin* createPlugin()
{
    return new AmpVibePlugin();
}

END_NAMESPACE_DISTRHO
