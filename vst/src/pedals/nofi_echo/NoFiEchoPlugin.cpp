/*
 * NoFiEcho - DE7-style stereo lo-fi echo for the game's Pedal_NoFiEcho.
 *
 * Local reference: pedals/nofi echo.png. The schematic is an Ibanez DE7 style
 * digital delay/echo: switched delay range/mode, delay level, repeat, output
 * buffering, and stereo output. the game exposes Time, Feedback, and Mix, so
 * the hidden mode is fixed to a warm Echo voice with stereo spread.
 */
#include "DistrhoPlugin.hpp"
#include "NoFiEchoParams.h"
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

class NoFiEchoCore
{
    float sampleRate = 48000.0f;
    float time = kNoFiEchoDef[kTime];
    float feedback = kNoFiEchoDef[kFeedback];
    float mix = kNoFiEchoDef[kMix];

    DelayBuffer delayL;
    DelayBuffer delayR;
    Biquad inHpL;
    Biquad inHpR;
    Biquad inLpL;
    Biquad inLpR;
    Biquad loopHpL;
    Biquad loopHpR;
    Biquad loopLpL;
    Biquad loopLpR;
    Biquad wetLpL;
    Biquad wetLpR;

    float delaySmoothL = 360.0f;
    float delaySmoothR = 382.0f;
    float fbMemL = 0.0f;
    float fbMemR = 0.0f;
    float envL = 0.0f;
    float envR = 0.0f;
    float lofiL = 0.0f;
    float lofiR = 0.0f;
    float wobblePhase = 0.0f;
    float compCoef = 0.0f;
    uint32_t noiseState = 0x5e7ec0edu;

    float currentDelayMs() const
    {
        // Existing the game mapping stores Time as milliseconds / 2000.
        const float ms = time * 2000.0f;
        return std::fmax(24.0f, std::fmin(ms, 1500.0f));
    }

    float noise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01(delayMs / 1200.0f);
        const float fb = smoothstep(feedback);

        inHpL.setHighPass(sampleRate, 28.0f, 0.70f);
        inHpR.setHighPass(sampleRate, 28.0f, 0.70f);
        inLpL.setLowPass(sampleRate, 7600.0f - 900.0f * t, 0.68f);
        inLpR.setLowPass(sampleRate, 7600.0f - 900.0f * t, 0.68f);
        loopHpL.setHighPass(sampleRate, 62.0f + 54.0f * fb, 0.63f);
        loopHpR.setHighPass(sampleRate, 62.0f + 54.0f * fb, 0.63f);
        loopLpL.setLowPass(sampleRate, 4350.0f - 1220.0f * t - 760.0f * fb, 0.56f);
        loopLpR.setLowPass(sampleRate, 4050.0f - 1120.0f * t - 720.0f * fb, 0.56f);
        wetLpL.setLowPass(sampleRate, 5200.0f - 820.0f * t, 0.60f);
        wetLpR.setLowPass(sampleRate, 5000.0f - 780.0f * t, 0.60f);
        compCoef = onePoleCoeff(20.0f, sampleRate);
    }

    float degrade(float x, float& hold, float delayMs, float side)
    {
        const float t = clamp01(delayMs / 1000.0f);
        const float grit = 0.18f + 0.52f * t + 0.22f * feedback;
        const float bitDepth = 10.0f - 4.0f * clamp01(t + 0.35f * feedback);
        const float steps = std::pow(2.0f, bitDepth);
        const float crushed = std::floor(x * steps + 0.5f) / steps;
        const float n = noise() * (0.00045f + 0.0024f * t) * (0.25f + 0.75f * mix);
        hold += onePoleCoeff(2600.0f + 600.0f * side, sampleRate) * (crushed - hold);
        return softClip((hold + n) * (1.0f + 0.16f * grit));
    }

public:
    void reset()
    {
        delayL.reset();
        delayR.reset();
        inHpL.reset();
        inHpR.reset();
        inLpL.reset();
        inLpR.reset();
        loopHpL.reset();
        loopHpR.reset();
        loopLpL.reset();
        loopLpR.reset();
        wetLpL.reset();
        wetLpR.reset();
        const float d = currentDelayMs();
        delaySmoothL = d;
        delaySmoothR = d + 18.0f;
        fbMemL = fbMemR = envL = envR = lofiL = lofiR = wobblePhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delayL.resize((int)(sampleRate * 1.58f) + 64);
        delayR.resize((int)(sampleRate * 1.58f) + 64);
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

    void process(float inL, float inR, float& outL, float& outR)
    {
        const float targetMs = currentDelayMs();
        delaySmoothL += onePoleCoeff(12.0f, sampleRate) * (targetMs - delaySmoothL);
        delaySmoothR += onePoleCoeff(10.0f, sampleRate) * ((targetMs * 1.035f + 13.0f) - delaySmoothR);

        wobblePhase += (0.10f + 0.055f * feedback) / sampleRate;
        if (wobblePhase >= 1.0f)
            wobblePhase -= 1.0f;
        const float wobbleA = std::sin(wobblePhase * 2.0f * kPi);
        const float wobbleB = std::sin((wobblePhase + 0.31f) * 2.0f * kPi);
        const float wobbleMs = 0.16f + 0.85f * clamp01(targetMs / 1200.0f);

        float dl = delayL.read((delaySmoothL + wobbleA * wobbleMs) * 0.001f * sampleRate);
        float dr = delayR.read((delaySmoothR + wobbleB * wobbleMs * 1.24f) * 0.001f * sampleRate);
        dl = loopLpL.process(loopHpL.process(dl));
        dr = loopLpR.process(loopHpR.process(dr));

        envL += compCoef * (std::fabs(dl) - envL);
        envR += compCoef * (std::fabs(dr) - envR);
        const float expL = 0.84f + 0.22f / (0.08f + envL);
        const float expR = 0.84f + 0.22f / (0.08f + envR);
        float wetL = wetLpL.process(degrade(dl * std::fmin(expL, 1.95f), lofiL, targetMs, 0.0f));
        float wetR = wetLpR.process(degrade(dr * std::fmin(expR, 1.95f), lofiR, targetMs, 1.0f));

        float xL = inLpL.process(inHpL.process(inL));
        float xR = inLpR.process(inHpR.process(inR));
        xL = softClip(xL * 1.08f);
        xR = softClip(xR * 1.08f);

        const float fb = 0.015f + 0.78f * smoothstep(feedback);
        const float cross = 0.18f + 0.18f * mix;
        const float regenL = softClip((wetL + fbMemR * cross) * (0.92f + 0.25f * feedback));
        const float regenR = softClip((wetR + fbMemL * cross) * (0.92f + 0.25f * feedback));
        fbMemL = regenL;
        fbMemR = regenR;

        delayL.write(softClip(xL + regenL * fb));
        delayR.write(softClip(xR + regenR * fb));

        const float dryLevel = 1.0f - 0.26f * mix;
        const float wetLevel = mix * (1.24f + 0.20f * feedback);
        outL = softClip(inL * dryLevel + wetL * wetLevel) * 0.99f;
        outR = softClip(inR * dryLevel + wetR * wetLevel) * 0.99f;
    }
};

class NoFiEchoPlugin : public Plugin
{
    NoFiEchoCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    NoFiEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kNoFiEchoDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NoFiEcho"; }
    const char* getDescription() const override { return "DE7 style stereo lo-fi echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'f', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kNoFiEchoNames[index];
        parameter.symbol = kNoFiEchoSymbols[index];
        parameter.ranges.min = kNoFiEchoMin[index];
        parameter.ranges.max = kNoFiEchoMax[index];
        parameter.ranges.def = kNoFiEchoDef[index];
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
            float yL = 0.0f;
            float yR = 0.0f;
            core.process(inL[i], inR[i], yL, yR);
            outL[i] = yL;
            outR[i] = yR;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoFiEchoPlugin)
};

Plugin* createPlugin()
{
    return new NoFiEchoPlugin();
}

END_NAMESPACE_DISTRHO
