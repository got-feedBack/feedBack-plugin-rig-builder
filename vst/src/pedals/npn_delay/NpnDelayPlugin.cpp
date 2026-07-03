/*
 * NpnDelay - Boss DM-2 style BBD delay for the game's Pedal_NPNDelay.
 *
 * Local reference: pedals/classic npn delay.pdf. The circuit has a short
 * MN3005/MN3205 BBD line, NE570 companding, dark low-pass repeat filtering,
 * NPN buffer/switching stages, and the DM-2 control set: Repeat Rate, Echo,
 * and Intensity. the game exposes Time, Feedback, and Mix. Some RS presets
 * push Time above the original DM-2 spec, so the model accepts a slightly
 * extended BBD range while keeping the repeats dark and compressed.
 */
#include "DistrhoPlugin.hpp"
#include "NpnDelayParams.h"
#include <cmath>
#include <cstdint>
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
    hz = std::fmax(6.0f, std::fmin(hz, nyquist));
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
        hz = std::fmax(6.0f, std::fmin(hz, sr * 0.45f));
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = std::fmax(6.0f, std::fmin(hz, sr * 0.45f));
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
        const int i2 = (i1 + 1) % size;
        const int im1 = (i0 + size - 1) % size;
        const float frac = pos - (float)i0;

        const float y0 = data[(size_t)im1];
        const float y1 = data[(size_t)i0];
        const float y2 = data[(size_t)i1];
        const float y3 = data[(size_t)i2];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
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

class NpnDelayCore
{
    float sampleRate = 48000.0f;
    float time = kNpnDelayDef[kTime];
    float feedback = kNpnDelayDef[kFeedback];
    float mix = kNpnDelayDef[kMix];

    DelayBuffer delay;
    Biquad inputHp;
    Biquad inputLp;
    Biquad loopHp;
    Biquad loopLp1;
    Biquad loopLp2;
    Biquad wetLp;

    float delaySmoothMs = 220.0f;
    float preEnv = 0.0f;
    float postEnv = 0.0f;
    float noise = 0.0f;
    float clockPhase = 0.0f;
    float compCoef = 0.0f;
    uint32_t noiseState = 0x46d2365bu;

    float currentDelayMs() const
    {
        // the game stores Time as milliseconds / 2000 in the VST state.
        const float ms = time * 2000.0f;
        return std::fmax(18.0f, std::fmin(ms, 420.0f));
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01((delayMs - 18.0f) / (420.0f - 18.0f));
        const float fb = smoothstep(feedback);

        inputHp.setHighPass(sampleRate, 34.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 6900.0f - 900.0f * t, 0.68f);
        loopHp.setHighPass(sampleRate, 78.0f + 58.0f * fb, 0.62f);
        loopLp1.setLowPass(sampleRate, 3650.0f - 1050.0f * t - 610.0f * fb, 0.58f);
        loopLp2.setLowPass(sampleRate, 3150.0f - 850.0f * t - 520.0f * fb, 0.54f);
        wetLp.setLowPass(sampleRate, 4200.0f - 700.0f * t, 0.64f);
        compCoef = onePoleCoeff(22.0f, sampleRate);
    }

    float randomNoise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const float n = ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
        noise += onePoleCoeff(1800.0f, sampleRate) * (n - noise);
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
        wetLp.reset();
        delaySmoothMs = currentDelayMs();
        preEnv = postEnv = noise = clockPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.46f) + 32);
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
        const float slewHz = 18.0f + 26.0f * (1.0f - clamp01(targetMs / 420.0f));
        delaySmoothMs += onePoleCoeff(slewHz, sampleRate) * (targetMs - delaySmoothMs);

        const float clockHz = 0.18f + 0.05f * feedback;
        clockPhase += clockHz / sampleRate;
        if (clockPhase >= 1.0f)
            clockPhase -= 1.0f;
        const float wobbleMs = std::sin(clockPhase * 2.0f * kPi) * (0.09f + 0.34f * clamp01(targetMs / 420.0f));

        float delayed = delay.read((delaySmoothMs + wobbleMs) * 0.001f * sampleRate);
        delayed = loopHp.process(delayed);
        delayed = loopLp1.process(delayed);
        delayed = loopLp2.process(delayed);

        postEnv += compCoef * (std::fabs(delayed) - postEnv);
        const float expand = 0.78f + 0.32f / (0.10f + postEnv);
        const float noiseAmt = (0.00028f + 0.00185f * clamp01(targetMs / 420.0f))
            * (0.28f + 0.72f * mix) * (0.55f + 0.45f * feedback);
        float wet = delayed * std::fmin(expand, 2.05f) + randomNoise() * noiseAmt;
        wet = wetLp.process(softClip(wet * (1.08f + 0.14f * feedback)));

        float x = inputHp.process(in);
        x = inputLp.process(x);
        preEnv += compCoef * (std::fabs(x) - preEnv);
        const float compress = 1.0f / (1.0f + 1.55f * preEnv);
        x = softClip(x * (1.26f + 0.18f * feedback) * (0.84f + 0.34f * compress));

        const float fbAmount = 0.01f + 0.91f * smoothstep(feedback);
        const float loopDrive = 1.02f + 0.28f * feedback;
        const float writeSample = softClip((x + wet * fbAmount) * loopDrive);
        delay.write(writeSample);

        const float wetLevel = mix * (1.18f + 0.26f * feedback);
        return wet * wetLevel;
    }
};

class NpnDelayPlugin : public Plugin
{
    NpnDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    NpnDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kNpnDelayDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NpnDelay"; }
    const char* getDescription() const override { return "DM-2 style BBD delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'p', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kNpnDelayNames[index];
        parameter.symbol = kNpnDelaySymbols[index];
        parameter.ranges.min = kNpnDelayMin[index];
        parameter.ranges.max = kNpnDelayMax[index];
        parameter.ranges.def = kNpnDelayDef[index];
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
            const float dryTrim = 1.0f - 0.11f * params[kMix];
            outL[i] = softClip(inL[i] * dryTrim + wet) * 0.99f;
            outR[i] = softClip(inR[i] * dryTrim + wet) * 0.99f;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NpnDelayPlugin)
};

Plugin* createPlugin()
{
    return new NpnDelayPlugin();
}

END_NAMESPACE_DISTRHO
