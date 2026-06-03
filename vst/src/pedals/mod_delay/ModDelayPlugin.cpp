/*
 * ModDelay - modulated digital delay for Rocksmith's Pedal_ModDelay.
 * Reference: local Ibanez DLL10 schematics used as a practical stand-in for
 * the DML10-style Rocksmith pedal. The source circuit has delay time, regen,
 * speed, and width controls around a digital delay line, with companding and
 * filtered repeats. Rocksmith exposes Time, Feedback, Mix, Rate, and Depth.
 */
#include "DistrhoPlugin.hpp"
#include "ModDelayParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeff(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
};

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resize(int samples)
    {
        if (samples < 4)
            samples = 4;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(float delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 3)
            return 0.0f;
        if (delaySamples < 1.0f)
            delaySamples = 1.0f;
        if (delaySamples > (float)(size - 3))
            delaySamples = (float)(size - 3);

        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        while (pos >= (float)size)
            pos -= (float)size;

        const int i0 = (int)std::floor(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }

    void write(float x)
    {
        if (data.empty())
            return;
        data[(size_t)writeIndex] = x;
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

} // namespace

class ModDelayCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float time = kModDelayDef[kTime];
    float feedback = kModDelayDef[kFeedback];
    float mix = kModDelayDef[kMix];
    float rate = kModDelayDef[kRate];
    float depth = kModDelayDef[kDepth];

    DelayBuffer delay;
    Biquad inputHp;
    Biquad inputLp;
    Biquad repeatHp;
    Biquad repeatLp;

    float lfoPhase = 0.0f;
    float delaySmooth = 360.0f;
    float fbMemory = 0.0f;
    float comp = 0.0f;
    float compA = 0.0f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        inputHp.setHighPass(sampleRate, 38.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 7600.0f - 1500.0f * d, 0.72f);
        repeatHp.setHighPass(sampleRate, 95.0f + 70.0f * d, 0.66f);
        repeatLp.setLowPass(sampleRate, 5600.0f - 1850.0f * d - 650.0f * feedback, 0.64f);
        compA = onePoleCoeff(16.0f, sampleRate);
    }

    float currentDelayMs() const
    {
        return 20.0f + time * (900.0f - 20.0f);
    }

    float currentRateHz() const
    {
        if (rate <= 0.0001f)
            return 0.0f;
        return 0.03f + rate * 3.47f;
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset;
    }

    void reset()
    {
        delay.reset();
        inputHp.reset();
        inputLp.reset();
        repeatHp.reset();
        repeatLp.reset();
        lfoPhase = phaseOffset;
        delaySmooth = currentDelayMs();
        fbMemory = comp = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 1.15f) + 16);
        reset();
    }

    void setTime(float v)
    {
        time = clamp01(v);
    }

    void setFeedback(float v)
    {
        feedback = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setRate(float v)
    {
        rate = clamp01(v);
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        float x = inputHp.process(in);
        x = inputLp.process(x);

        const float rateHz = currentRateHz();
        if (rateHz > 0.0f)
        {
            lfoPhase += rateHz / sampleRate;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;
        }

        const float lfo = std::sin((lfoPhase + phaseOffset) * 2.0f * kPi);
        const float baseMs = currentDelayMs();
        const float widthMs = depth * (2.5f + 28.0f * (1.0f - 0.35f * time));
        const float targetMs = baseMs + lfo * widthMs;
        delaySmooth += onePoleCoeff(22.0f + 44.0f * rate, sampleRate) * (targetMs - delaySmooth);

        const float delayed = delay.read(delaySmooth * 0.001f * sampleRate);
        float repeat = repeatHp.process(delayed);
        repeat = repeatLp.process(repeat);

        comp += compA * (std::fabs(repeat) - comp);
        const float compGain = 1.0f / (1.0f + 1.65f * comp);
        repeat *= compGain;

        const float fbAmount = 0.02f + feedback * 0.86f;
        const float regen = std::tanh((repeat + fbMemory * 0.18f) * 1.08f);
        fbMemory = regen;

        float writeSample = x + regen * fbAmount;
        writeSample = std::tanh(writeSample * (0.95f + 0.18f * feedback));
        delay.write(writeSample);

        const float wetTone = repeat * (0.80f + 0.16f * depth);
        const float dryLevel = 1.0f - 0.56f * mix;
        const float wetLevel = mix * (0.78f + 0.16f * feedback);
        float y = in * dryLevel + wetTone * wetLevel;
        y = std::tanh(y * 0.94f) * 0.97f;
        return y;
    }
};

class ModDelayPlugin : public Plugin
{
    ModDelayCore left;
    ModDelayCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTime(params[kTime]);
        right.setTime(params[kTime]);
        left.setFeedback(params[kFeedback]);
        right.setFeedback(params[kFeedback]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    ModDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kModDelayDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.25f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ModDelay"; }
    const char* getDescription() const override { return "Modulated digital delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('M', 'd', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kModDelayNames[index];
        parameter.symbol = kModDelaySymbols[index];
        parameter.ranges.min = kModDelayMin[index];
        parameter.ranges.max = kModDelayMax[index];
        parameter.ranges.def = kModDelayDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = clamp01(value);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModDelayPlugin)
};

Plugin* createPlugin()
{
    return new ModDelayPlugin();
}

END_NAMESPACE_DISTRHO
