/*
 * Deja Chorus - Deja Vibe / Uni-Vibe style optical chorus.
 *
 * References: pedals/chorus 20/ElectroVibe-PedalPCB.pdf and the Pisotones
 * UniVibe document. The model follows the transistor preamp, four component
 * BJT/LDR phase stages and incandescent lamp driver. Legacy selector parameters
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
    rbmod::LowPass toneLp;
    rbmod::LowPass airLp;
    rbmod::HighPass outputHp;
    rbmod::LampLdrModel lamp;
    rbmod::TransistorVibeStage stages[4];

    float currentRateHz() const
    {
        // New reference anchors at 0/2/5/7/10 put the sweep at approximately
        // 1.0/2.3/4.5/5.9/8.0 Hz. The real control is effectively linear over
        // that operating range.
        return 1.0f + 7.0f * rbmod::clamp01(speedNow);
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
        toneLp.setHz(700.0f, sampleRate);
        airLp.setHz(4000.0f, sampleRate);
        outputHp.setHz(18.0f, sampleRate);
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
        toneLp.reset();
        airLp.reset();
        outputHp.reset();
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
        const float spread[4] = { 1.00f, 0.92f, 1.10f, 0.97f };
        for (int i = 0; i < 4; ++i)
        {
            wet = stages[i].process(wet, 10.0f * ldrR * spread[i]);
        }
        wet = outputLp.process(wet);
        dc += 0.00035f * (wet - dc);
        wet -= dc;

        // Chorus mode is the external dry/phase-path mixer after the four-stage
        // ladder. The emitter/collector sum inside each stage creates that
        // stage's all-pass transfer; it does not replace this final dry branch.
        // A wet-heavy 0.15/0.85 blend was effectively close to Vibrato and hid
        // the moving comb notches. The new references put the voltage blend at
        // 0.35/0.65: enough direct path for Chorus without the deep,
        // tremolo-like nulls produced by a literal equal-amplitude sum.
        const float chorus = 0.35f * x + 0.65f * wet;
        const float q = rbmod::clamp01(intensityNow);
        const float effectDepth = rbmod::clamp01(2.0f * q);
        const float mixed = dryIn + effectDepth * (chorus - dryIn);

        // The reference output has a broad rising response rather than the
        // dark global low-pass of the old model. Keep this transistor/output
        // voicing after the optical mixer so it does not alter LDR excursion.
        const float low = toneLp.process(mixed);
        const float lowGain = 1.40f - 0.60f * q;
        float voiced = lowGain * low + 1.30f * (mixed - low);
        const float airBase = airLp.process(voiced);
        const float airGain = 2.35f - 0.70f * q;
        voiced = airBase + airGain * (voiced - airBase);

        // The analog output stage compensates the different cancellation
        // produced by Intensity and sweep rate without changing LDR travel.
        const float s = rbmod::clamp01(speedNow);
        const float speedCompDb = q * ((-2.168f + 5.925f * s - 8.226f * s * s)
                                      + q * (0.536f - 6.882f * s + 9.380f * s * s));
        const float speedComp = std::pow(10.0f, speedCompDb / 20.0f);
        // Keep level calibration separate from the optical network so it
        // cannot alter the sweep itself. The revised renders stay below rail.
        const float characterGain = 0.65f + 2.00f * std::pow(q, 1.55f);
        const float defaultTaper = rbmod::audioTaper(kChorus20Def[kVolume]);
        const float outGain = rbmod::audioTaper(volumeNow) / defaultTaper;
        const float y = outputHp.process(voiced) * characterGain * speedComp * outGain;
        return rbmod::clamp(y, -1.0f, 1.0f);
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
    uint32_t getVersion() const override { return d_version(1, 7, 0); }
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
