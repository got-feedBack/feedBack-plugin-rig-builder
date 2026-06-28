/*
 * Deja Chorus - Deja Vibe / Uni-Vibe style optical chorus.
 *
 * References: pedals/chorus 20/ElectroVibe-PedalPCB.pdf and the Pisotones
 * UniVibe document. The model follows the transistor preamp, four LDR-driven
 * all-pass stages, incandescent lamp driver and Chorus/Vibrato output switch.
 */
#include "DistrhoPlugin.hpp"
#include "Chorus20Params.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class VibeCore
{
    float sampleRate = 48000.0f;
    float intensity = kChorus20Def[kIntensity];
    float speed1 = kChorus20Def[kSpeed1];
    float speed2 = kChorus20Def[kSpeed2];
    float speedSelect = kChorus20Def[kSpeedSelect];
    float volume = kChorus20Def[kVolume];
    float mode = kChorus20Def[kMode];

    float phase = 0.0f;
    float dc = 0.0f;
    float preBias = 0.0f;

    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;
    rbmod::LampLdrModel lamp;
    rbmod::FirstOrderAllPass stages[4];

    float currentRateHz() const
    {
        const float s = speedSelect >= 0.5f ? speed2 : speed1;
        return 0.070f + 6.80f * std::pow(rbmod::clamp01(s), 2.05f);
    }

    void configureStages()
    {
        // ElectroVibe phase network values from the PedalPCB schematic/BOM.
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
        outputLp.setHz(7600.0f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        lamp.setSampleRate(sampleRate);
        configureStages();
        updateFilters();
        reset();
    }

    void reset()
    {
        inputHp.reset();
        inputLp.reset();
        outputLp.reset();
        lamp.reset();
        for (int i = 0; i < 4; ++i)
            stages[i].reset();
        phase = 0.0f;
        dc = 0.0f;
        preBias = 0.0f;
    }

    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setSpeed1(float v) { speed1 = rbmod::clamp01(v); }
    void setSpeed2(float v) { speed2 = rbmod::clamp01(v); }
    void setSpeedSelect(float v) { speedSelect = v >= 0.5f ? 1.0f : 0.0f; }
    void setVolume(float v) { volume = rbmod::clamp01(v); }
    void setMode(float v) { mode = v >= 0.5f ? 1.0f : 0.0f; }

    float process(float in)
    {
        phase += currentRateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        // Analog LFO into lamp driver. The Q1/Q2 oscillator is close to sine
        // but the lamp driver/bulb inertia makes the bright half wider.
        const float lfo = 0.5f + 0.5f * std::sin(rbmod::kTwoPi * phase);
        const float inten = std::pow(rbmod::clamp01(intensity), 1.20f);
        const float drive = rbmod::clamp01(0.12f + (0.22f + 0.76f * inten) * lfo);
        const float light = lamp.processLight(drive);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        preBias += 0.00055f * (x - preBias);
        x -= preBias;
        x = rbmod::softClip(x * 1.16f) * 0.93f;

        float wet = x;
        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);
        const float spread[4] = { 1.00f, 0.82f, 1.18f, 0.96f };
        for (int i = 0; i < 4; ++i)
        {
            const float stageR = 4700.0f + ldrR * spread[i];
            wet = stages[i].process(wet, stageR);
            wet = rbmod::softClip(wet * 1.035f);
        }
        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        const float chorusMode = 1.0f - mode; // 0 = chorus, 1 = vibrato
        const float dry = x * (0.78f * chorusMode);
        const float wetLevel = mode >= 0.5f ? 0.96f : 0.86f;
        const float mixed = mode >= 0.5f ? wet * wetLevel
                                         : dry - wet * wetLevel;
        const float outGain = 0.18f + 1.65f * rbmod::audioTaper(volume);
        return rbmod::softClip(mixed * outGain) * 0.90f;
    }
};

class Chorus20Plugin : public Plugin
{
    VibeCore left;
    VibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setIntensity(params[kIntensity]);
        right.setIntensity(params[kIntensity]);
        left.setSpeed1(params[kSpeed1]);
        right.setSpeed1(params[kSpeed1]);
        left.setSpeed2(params[kSpeed2]);
        right.setSpeed2(params[kSpeed2]);
        left.setSpeedSelect(params[kSpeedSelect]);
        right.setSpeedSelect(params[kSpeedSelect]);
        left.setVolume(params[kVolume]);
        right.setVolume(params[kVolume]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
    }

public:
    Chorus20Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChorus20Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Chorus20"; }
    const char* getDescription() const override { return "Deja Vibe style optical chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'h', '2', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kSpeedSelect || index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kChorus20Names[index];
        parameter.symbol = kChorus20Symbols[index];
        parameter.ranges.min = kChorus20Min[index];
        parameter.ranges.max = kChorus20Max[index];
        parameter.ranges.def = kChorus20Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kSpeedSelect || index == (uint32_t)kMode)
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
            outL[i] = left.process(inL[i]);
            outR[i] = right.process(inR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Chorus20Plugin)
};

Plugin* createPlugin()
{
    return new Chorus20Plugin();
}

END_NAMESPACE_DISTRHO
