/*
 * AnalogDelay - MF-104 style BBD delay for Rocksmith's Pedal_AnalogueDelay.
 *
 * Local reference: pedals/analog delay.pdf, Moog MF-104/MF-104Z schematics:
 * input drive, SA572 compander, chained MN3008 BBDs, dark filtering, analog
 * feedback loop, and dry/wet VCA mixing. Rocksmith exposes Time, Feedback,
 * and Mix, so the remaining MF-104 controls are fixed internally.
 */
#include "DistrhoPlugin.hpp"
#include "AnalogDelayParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeff(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    hz = std::fmax(8.0f, std::fmin(hz, nyquist));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float softClip(float x)
{
    return std::tanh(x);
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
        hz = std::fmax(8.0f, std::fmin(hz, sr * 0.45f));
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = std::fmax(8.0f, std::fmin(hz, sr * 0.45f));
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
        if (samples < 8)
            samples = 8;
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
        if (size <= 4)
            return 0.0f;

        delaySamples = std::fmax(1.0f, std::fmin(delaySamples, (float)(size - 3)));
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

class AnalogDelayCore
{
    float sampleRate = 48000.0f;
    float time = kAnalogDelayDef[kTime];
    float feedback = kAnalogDelayDef[kFeedback];
    float mix = kAnalogDelayDef[kMix];

    DelayBuffer delay;
    Biquad inputHp;
    Biquad inputLp;
    Biquad loopHp;
    Biquad loopLp1;
    Biquad loopLp2;

    float delaySmoothMs = 280.0f;
    float compEnv = 0.0f;
    float noise = 0.0f;
    float driftPhase = 0.0f;
    float compCoef = 0.0f;
    uint32_t noiseState = 0x1234567u;

    float currentDelayMs() const
    {
        // Existing Rocksmith mapping stores Time as milliseconds / 2000.
        const float ms = time * 2000.0f;
        return std::fmax(8.0f, std::fmin(ms, 1050.0f));
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01(delayMs / 1000.0f);
        const float fb = smoothstep(feedback);

        inputHp.setHighPass(sampleRate, 24.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 8200.0f - 1600.0f * t, 0.68f);
        loopHp.setHighPass(sampleRate, 72.0f + 72.0f * fb, 0.63f);
        loopLp1.setLowPass(sampleRate, 4700.0f - 1850.0f * t - 700.0f * fb, 0.58f);
        loopLp2.setLowPass(sampleRate, 3900.0f - 1350.0f * t - 560.0f * fb, 0.54f);
        compCoef = onePoleCoeff(18.0f, sampleRate);
    }

    float randomBbdNoise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const float n = ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
        noise += onePoleCoeff(2200.0f, sampleRate) * (n - noise);
        return noise;
    }

public:
    void reset()
    {
        delay.reset();
        inputHp.reset();
        inputLp.reset();
        loopHp.reset();
        loopLp1.reset();
        loopLp2.reset();
        delaySmoothMs = currentDelayMs();
        compEnv = noise = driftPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 1.12f) + 32);
        reset();
    }

    void setTime(float v)
    {
        time = clamp01(v);
        updateFilters();
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

    float process(float in)
    {
        const float targetMs = currentDelayMs();
        const float slewHz = 9.0f + 18.0f * (1.0f - clamp01(targetMs / 1000.0f));
        delaySmoothMs += onePoleCoeff(slewHz, sampleRate) * (targetMs - delaySmoothMs);

        driftPhase += (0.08f + 0.07f * feedback) / sampleRate;
        if (driftPhase >= 1.0f)
            driftPhase -= 1.0f;
        const float clockDriftMs = std::sin(driftPhase * 2.0f * kPi) * (0.18f + 1.25f * clamp01(targetMs / 1000.0f));

        float delayed = delay.read((delaySmoothMs + clockDriftMs) * 0.001f * sampleRate);
        delayed = loopHp.process(delayed);
        delayed = loopLp1.process(delayed);
        delayed = loopLp2.process(delayed);

        compEnv += compCoef * (std::fabs(delayed) - compEnv);
        const float expand = 1.0f + 0.35f / (0.06f + compEnv);
        const float bbdNoise = randomBbdNoise() * (0.00055f + 0.0028f * clamp01(targetMs / 1000.0f)) * (0.35f + 0.65f * mix);
        float wet = softClip((delayed + bbdNoise) * 1.06f) * (0.72f + 0.28f * std::fmin(expand, 2.1f));

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = 0.91f * x + 0.09f * softClip(x * 1.55f);

        const float fbAmount = 0.015f + 0.82f * smoothstep(feedback);
        const float loopDrive = 0.96f + 0.24f * feedback;
        const float writeSample = softClip((x + wet * fbAmount) * loopDrive);
        delay.write(writeSample);

        const float wetLevel = mix * (1.14f + 0.18f * feedback);
        return wet * wetLevel;
    }
};

class AnalogDelayPlugin : public Plugin
{
    AnalogDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    AnalogDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAnalogDelayDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AnalogDelay"; }
    const char* getDescription() const override { return "MF-104 style BBD analog delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'n', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAnalogDelayNames[index];
        parameter.symbol = kAnalogDelaySymbols[index];
        parameter.ranges.min = kAnalogDelayMin[index];
        parameter.ranges.max = kAnalogDelayMax[index];
        parameter.ranges.def = kAnalogDelayDef[index];
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
            const float monoIn = 0.5f * (inL[i] + inR[i]);
            const float wet = core.process(monoIn);
            const float dryTrim = 1.0f - 0.22f * params[kMix];
            outL[i] = softClip(inL[i] * dryTrim + wet) * 0.99f;
            outR[i] = softClip(inR[i] * dryTrim + wet) * 0.99f;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogDelayPlugin)
};

Plugin* createPlugin()
{
    return new AnalogDelayPlugin();
}

END_NAMESPACE_DISTRHO
