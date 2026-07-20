/*
 * AmpVibe - Uni-Vibe style optical modulation for Pedal_AmpVibe.
 *
 * Local references: pedals/ampvibe.jpg, pedals/chorus 20/ElectroVibe-PedalPCB.pdf
 * and the Pisotones Uni-Vibe notes. The DSP follows the transistor preamp,
 * four LDR-controlled all-pass stages and incandescent lamp inertia. The
 * chassis exposes Intensity and Volume; Speed remains available to the host as
 * the real external foot-controller input. The compatibility Mode parameter is
 * fixed internally to Chorus.
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
    float speedNow = speed;
    float intensityNow = intensity;
    float volumeNow = volume;
    float smoothA = 1.0f;

    float phase = 0.0f;
    float dc = 0.0f;
    float preBias = 0.0f;

    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;
    rbmod::LowPass toneLp;
    rbmod::LowPass airLp;
    rbmod::HighPass outputHp;
    rbmod::LampLdrModel lamp;
    rbmod::TransistorVibeStage stages[4];

    float currentRateHz() const
    {
        // Reference renders anchor the external speed pedal at roughly
        // 0.10/4.95/10.0 Hz for minimum/half/maximum travel.
        return 0.10f + 9.90f * rbmod::clamp01(speedNow);
    }

    void configureStages()
    {
        // ElectroVibe/Uni-Vibe phase network caps. Use the component-domain
        // transistor stages rather than ideal all-pass sections so collector,
        // emitter loading and the real stage magnitude are retained.
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
        inputLp.setHz(13000.0f, sampleRate);
        outputLp.setHz(11000.0f, sampleRate);
        toneLp.setHz(700.0f, sampleRate);
        airLp.setHz(4500.0f, sampleRate);
        outputHp.setHz(18.0f, sampleRate);
    }

public:
    void reset()
    {
        inputHp.reset();
        inputLp.reset();
        outputLp.reset();
        toneLp.reset();
        airLp.reset();
        outputHp.reset();
        lamp.reset();
        for (int i = 0; i < 4; ++i)
            stages[i].reset();
        phase = 0.0f;
        dc = 0.0f;
        preBias = 0.0f;
        speedNow = speed;
        intensityNow = intensity;
        volumeNow = volume;
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        // The supplied renders retain almost full optical sweep at 4.95 Hz and
        // a clearly measurable sweep at 10 Hz. The generic slow CdS defaults
        // suppress both, so use the faster lamp/cell pair measured here.
        lamp.setTimeConstants(5.0f, 18.0f, 3.0f, 22.0f);
        lamp.setSampleRate(sampleRate);
        configureStages();
        updateFilters();
        reset();
    }

    void setSpeed(float v) { speed = rbmod::clamp01(v); }
    void setIntensity(float v) { intensity = rbmod::clamp01(v); }
    void setVolume(float v) { volume = rbmod::clamp01(v); }
    void setMode(float) {}

    float process(float in)
    {
        speedNow += smoothA * (speed - speedNow);
        intensityNow += smoothA * (intensity - intensityNow);
        volumeNow += smoothA * (volume - volumeNow);

        phase += currentRateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float lfo = 0.5f + 0.5f * std::sin(rbmod::kTwoPi * phase);
        // Intensity changes optical excursion, not dry/wet balance. The new
        // references retain the same long-term spectrum and level at minimum,
        // half and maximum while their phase trajectory changes.
        const float inten = rbmod::clamp01(intensityNow);
        // Keep the lamp inside the measured optical window. The previous
        // 0.26..1.00 maximum-intensity drive crossed the all-pass phase range
        // more than once, making the correlation reverse direction above
        // half Intensity. The references move monotonically from the static
        // phase path toward the wider sweep.
        const float drive = rbmod::clamp01(0.15f + inten * (0.06f + 0.10f * lfo));
        const float light = lamp.processLight(drive);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        preBias += 0.00050f * (x - preBias);
        x -= preBias;
        x = rbmod::softClip(x * 1.08f) / 1.08f;

        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);
        const float spread[4] = { 1.00f, 0.92f, 1.10f, 0.97f };
        float wet = x;
        for (int i = 0; i < 4; ++i)
        {
            wet = stages[i].process(wet, ldrR * spread[i]);
        }

        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        // The canonical two-control Uni-Vibe reference is the phase path. A
        // dry crossfade made minimum Intensity behave like bypass and changed
        // level by almost 5 dB across the knob, neither of which is present in
        // the reference renders.
        // Chorus output is the phase ladder summed against a smaller direct
        // branch. The fixed subtractive branch sets the reference's negative
        // correlation; Intensity only changes the optical trajectory.
        float y = wet - 0.18f * x;

        // Fixed preamp/output voicing measured in all three references. It is
        // independent of Intensity: low frequencies are lightly attenuated,
        // while the upper presence and air remain open.
        const float low = toneLp.process(y);
        y = 0.74f * low + 1.15f * (y - low);
        const float airBase = airLp.process(y);
        const float airGain = 3.42f - 1.28f * inten;
        y = airBase + airGain * (y - airBase);

        const float gainDb = 0.28f - 0.81f * inten + 2.14f * inten * inten;
        const float characterGain = std::pow(10.0f, gainDb / 20.0f);
        const float defaultTaper = rbmod::audioTaper(kAmpVibeDef[kVolume]);
        const float outGain = rbmod::audioTaper(volumeNow) / defaultTaper;
        y = outputHp.process(y);
        return y * characterGain * outGain;
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
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'm', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger | kParameterIsHidden;
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
        params[index] = index == (uint32_t)kMode ? 0.0f : rbmod::clamp01(value);
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
