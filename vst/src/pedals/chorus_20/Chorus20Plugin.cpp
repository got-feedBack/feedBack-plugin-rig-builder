/*
 * Deja Chorus - Deja Vibe / Uni-Vibe style optical chorus.
 *
 * References: pedals/chorus 20/ElectroVibe-PedalPCB.pdf and the Pisotones
 * UniVibe document. The model follows the transistor preamp, four LDR-driven
 * all-pass stages and incandescent lamp driver. Legacy selector parameters
 * remain at their original ids for song compatibility, but this pedal is fixed
 * to Chorus and uses a single visible Speed control.
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
    float volume = kChorus20Def[kVolume];
    float intensityNow = intensity;
    float speedNow = speed1;
    float volumeNow = volume;
    float smoothA = 1.0f;

    float phase = 0.0f;
    float dc = 0.0f;
    float preBias = 0.0f;

    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;
    rbmod::LowPass outputBass;
    rbmod::LampLdrModel lamp;
    rbmod::FirstOrderAllPass stages[4];

    float currentRateHz() const
    {
        // Measured DejaVibe anchors: about 0.10, 4.95 and 10.0 Hz.
        return 0.10f + 9.90f * rbmod::clamp01(speedNow);
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
        // Raised from 9 k / 7.6 k: the pair (plus the dry−wet comb) rolled the
        // top off from ~1 kHz and left the vibe sounding muffled/dark. A
        // Uni-Vibe is warm, not lifeless — keep some air up top.
        inputLp.setHz(13000.0f, sampleRate);
        outputLp.setHz(11000.0f, sampleRate);
        outputBass.setHz(600.0f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        lamp.setTimeConstants(5.0f, 18.0f, 3.0f, 22.0f);
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
        outputBass.reset();
        lamp.reset();
        for (int i = 0; i < 4; ++i)
            stages[i].reset();
        phase = 0.0f;
        dc = 0.0f;
        preBias = 0.0f;
        intensityNow = intensity;
        speedNow = speed1;
        volumeNow = volume;
    }

    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setSpeed1(float v) { speed1 = rbmod::clamp01(v); }
    void setSpeed2(float) {}
    void setSpeedSelect(float) {}
    void setVolume(float v) { volume = rbmod::clamp01(v); }
    void setMode(float) {}

    float process(float in)
    {
        intensityNow += smoothA * (intensity - intensityNow);
        speedNow += smoothA * (speed1 - speedNow);
        volumeNow += smoothA * (volume - volumeNow);

        phase += currentRateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        // Analog LFO into lamp driver. The Q1/Q2 oscillator is close to sine
        // but the lamp driver/bulb inertia makes the bright half wider.
        const float lfo = 0.5f + 0.5f * std::sin(rbmod::kTwoPi * phase);
        const float inten = 1.0f - std::exp(-5.0f * rbmod::clamp01(intensityNow));
        const float drive = rbmod::clamp01(0.10f + inten * (0.16f + 0.74f * lfo));
        const float light = lamp.processLight(drive);

        const float dryIn = in;
        float x = inputHp.process(in);
        x = inputLp.process(x);
        preBias += 0.00055f * (x - preBias);
        x -= preBias;
        x = rbmod::softClip(x * 1.08f) / 1.08f;

        float wet = x;
        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);
        const float spread[4] = { 1.00f, 0.82f, 1.18f, 0.96f };
        for (int i = 0; i < 4; ++i)
        {
            const float stageR = 4700.0f + 16.0f * ldrR * spread[i];
            wet = stages[i].process(wet, stageR);
        }
        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        // Chorus mode sums dry and phase-shifted paths through equal branches.
        // Explicitly interpolate from dry so Intensity=0 matches the reference
        // instead of leaving a static comb filter active.
        const float chorus = 0.52f * x + 0.52f * wet;
        const float mixed = dryIn + inten * (chorus - dryIn);

        // Per-channel renders put the dry minimum around -2 dB and useful
        // Intensity around +9.5 dB. The analog output stage also compensates
        // the deeper cancellation produced at faster sweeps.
        const float s = rbmod::clamp01(speedNow);
        const float speedCompDb = inten * (0.85f - 0.90f * s + 0.37f * s * s);
        const float speedComp = std::pow(10.0f, speedCompDb / 20.0f);
        const float characterGain = 0.793f + 3.00f * inten;
        const float defaultTaper = rbmod::audioTaper(kChorus20Def[kVolume]);
        const float outGain = rbmod::audioTaper(volumeNow) / defaultTaper;
        float y = mixed * characterGain * speedComp * outGain;
        y -= 0.28f * outputBass.process(y);
        return rbmod::clamp(y,
                            -1.0f, 1.0f);
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
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'h', '2', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kSpeedSelect || index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsHidden;
        if (index == (uint32_t)kSpeed2)
            parameter.hints |= kParameterIsHidden;
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
            ? 0.0f : rbmod::clamp01(value);
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
