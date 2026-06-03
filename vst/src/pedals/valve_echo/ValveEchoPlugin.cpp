/*
 * ValveEcho - Binson Echorec-style valve drum echo for Pedal_ValveEcho.
 *
 * Local references: pedals/valveecho_1.png and valveecho_2.png. Both are
 * Binson Echorec PE603T schematics: ECC83/ECC82 tube stages around a magnetic
 * drum echo with multiple playback heads and feedback switching. Rocksmith
 * exposes Time, Feedback, and Mix; hidden head/mode controls are fixed to a
 * musical multi-head echo.
 */
#include "DistrhoPlugin.hpp"
#include "ValveEchoParams.h"
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

static inline float valveStage(float x, float drive, float bias)
{
    const float ref = std::tanh(bias);
    const float y = std::tanh(x * drive + bias) - ref;
    return y * (1.0f / std::fmax(0.35f, drive * 0.72f));
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

class ValveEchoCore
{
    float sampleRate = 48000.0f;
    float time = kValveEchoDef[kTime];
    float feedback = kValveEchoDef[kFeedback];
    float mix = kValveEchoDef[kMix];

    DelayBuffer drum;
    Biquad inputHp;
    Biquad inputLp;
    Biquad loopHp;
    Biquad loopLp1;
    Biquad loopLp2;
    Biquad wetLpL;
    Biquad wetLpR;

    float delaySmoothMs = 450.0f;
    float fbMemory = 0.0f;
    float drumMemory = 0.0f;
    float env = 0.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float compCoef = 0.0f;
    uint32_t noiseState = 0x9e3779b9u;

    float currentDelayMs() const
    {
        // Existing Rocksmith mapping stores Time as milliseconds / 2000.
        const float ms = time * 2000.0f;
        return std::fmax(70.0f, std::fmin(ms, 1200.0f));
    }

    float noise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01(delayMs / 1000.0f);
        const float fb = smoothstep(feedback);

        inputHp.setHighPass(sampleRate, 24.0f, 0.66f);
        inputLp.setLowPass(sampleRate, 7200.0f - 900.0f * t, 0.68f);
        loopHp.setHighPass(sampleRate, 72.0f + 64.0f * fb, 0.58f);
        loopLp1.setLowPass(sampleRate, 5200.0f - 1450.0f * t - 700.0f * fb, 0.55f);
        loopLp2.setLowPass(sampleRate, 4600.0f - 1300.0f * t - 560.0f * fb, 0.52f);
        wetLpL.setLowPass(sampleRate, 6200.0f - 900.0f * t, 0.62f);
        wetLpR.setLowPass(sampleRate, 5900.0f - 900.0f * t, 0.62f);
        compCoef = onePoleCoeff(15.0f, sampleRate);
    }

    float headRead(float ratio, float offsetMs, float wobbleMs)
    {
        const float d = std::fmax(8.0f, delaySmoothMs * ratio + offsetMs + wobbleMs);
        return drum.read(d * 0.001f * sampleRate);
    }

public:
    void reset()
    {
        drum.reset();
        inputHp.reset();
        inputLp.reset();
        loopHp.reset();
        loopLp1.reset();
        loopLp2.reset();
        wetLpL.reset();
        wetLpR.reset();
        delaySmoothMs = currentDelayMs();
        fbMemory = drumMemory = env = wowPhase = flutterPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        drum.resize((int)(sampleRate * 1.28f) + 64);
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
        delaySmoothMs += onePoleCoeff(7.5f, sampleRate) * (targetMs - delaySmoothMs);

        wowPhase += (0.055f + 0.02f * feedback) / sampleRate;
        if (wowPhase >= 1.0f)
            wowPhase -= 1.0f;
        flutterPhase += 5.1f / sampleRate;
        if (flutterPhase >= 1.0f)
            flutterPhase -= 1.0f;

        const float wow = std::sin(wowPhase * 2.0f * kPi) * (0.42f + 1.8f * clamp01(targetMs / 1000.0f));
        const float flutter = std::sin(flutterPhase * 2.0f * kPi) * (0.08f + 0.22f * mix);

        const float h1 = headRead(0.33f, -3.0f, wow * 0.55f);
        const float h2 = headRead(0.53f,  1.0f, wow * 0.78f - flutter);
        const float h3 = headRead(0.76f,  4.0f, wow * 1.02f + flutter);
        const float h4 = headRead(1.00f,  0.0f, wow * 1.22f);

        float wetMono = h1 * 0.30f + h2 * 0.43f + h3 * 0.62f + h4 * 0.78f;
        wetMono = loopHp.process(wetMono);
        wetMono = loopLp1.process(wetMono);
        wetMono = loopLp2.process(wetMono);

        env += compCoef * (std::fabs(wetMono) - env);
        const float mediaLoss = 0.92f - 0.20f * clamp01(targetMs / 1000.0f);
        const float recovery = 0.86f + 0.20f / (0.08f + env);
        drumMemory += onePoleCoeff(1900.0f, sampleRate) * (wetMono - drumMemory);
        wetMono = valveStage(drumMemory * mediaLoss * std::fmin(recovery, 1.9f), 1.22f + 0.18f * feedback, 0.045f);
        wetMono += noise() * (0.00025f + 0.0012f * clamp01(targetMs / 1000.0f)) * (0.35f + 0.65f * mix);

        float wetL = wetLpL.process(wetMono + 0.25f * h2 - 0.10f * h3);
        float wetR = wetLpR.process(wetMono + 0.21f * h3 - 0.08f * h1);

        const float monoIn = 0.5f * (inL + inR);
        float x = inputLp.process(inputHp.process(monoIn));
        x = valveStage(x, 1.35f, 0.055f);

        const float fb = 0.018f + 0.74f * smoothstep(feedback);
        const float regen = valveStage(wetMono + fbMemory * 0.22f, 1.12f + 0.30f * feedback, 0.035f);
        fbMemory = regen;
        drum.write(softClip(x + regen * fb));

        const float wetLevel = mix * (1.20f + 0.30f * feedback);
        const float dryLevel = 1.0f - 0.18f * mix;
        outL = softClip(inL * dryLevel + wetL * wetLevel) * 0.99f;
        outR = softClip(inR * dryLevel + wetR * wetLevel) * 0.99f;
    }
};

class ValveEchoPlugin : public Plugin
{
    ValveEchoCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    ValveEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kValveEchoDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ValveEcho"; }
    const char* getDescription() const override { return "Binson Echorec style valve drum echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 'l', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kValveEchoNames[index];
        parameter.symbol = kValveEchoSymbols[index];
        parameter.ranges.min = kValveEchoMin[index];
        parameter.ranges.max = kValveEchoMax[index];
        parameter.ranges.def = kValveEchoDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValveEchoPlugin)
};

Plugin* createPlugin()
{
    return new ValveEchoPlugin();
}

END_NAMESPACE_DISTRHO
