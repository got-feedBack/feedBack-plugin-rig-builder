/*
 * Chorus - Boss CE-2 style BBD chorus for the game's Pedal_Chorus.
 * The local schematic is a CE-2: MN3007 BBD, MN3101 clock, TL022 LFO, and a
 * dark wet path mixed with dry. the game exposes Rate, Depth, and Mix.
 */
#include "DistrhoPlugin.hpp"
#include "ChorusParams.h"
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

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

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

class ChorusCore
{
    float sampleRate = 48000.0f;
    float rate = kChorusDef[kRate];
    float depth = kChorusDef[kDepth];
    float mix = kChorusDef[kMix];

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY = 0.0f;
    float compY = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float compA = 0.0f;

    float currentRateHz() const
    {
        return 0.08f * std::pow(75.0f, rate);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 34.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        preA = onePoleCoeffHz(7600.0f - 1300.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(4700.0f - 1300.0f * d, sampleRate);
        compA = onePoleCoeffHz(18.0f, sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

public:
    void reset()
    {
        delay.reset();
        lfoPhase = 0.0f;
        hpX1 = hpY1 = preY = bbdY = compY = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.080f));
        reset();
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

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    float process(float in)
    {
        const float rateHz = currentRateHz();
        lfoPhase += rateHz / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        const float lfo = 0.5f + 0.5f * std::sin(lfoPhase * 2.0f * kPi);
        const float d = 0.10f + 0.90f * smoothstep(depth);
        const float baseDelayMs = 8.4f + 1.8f * (1.0f - d);
        const float widthMs = 0.28f + 4.6f * d;
        const float delayMs = baseDelayMs + widthMs * (lfo - 0.5f);
        const float delaySamples = delayMs * 0.001f * sampleRate;

        float x = highPass(in);
        x = lowPass(x, preY, preA);
        x = 0.965f * x + 0.035f * softClip(x * (1.18f + 0.18f * d));

        const float wetRaw = delay.read(delaySamples);
        delay.write(x);

        // MN3007-style dark, slightly compressed wet path. This keeps the
        // chorus round instead of becoming a clean pitch vibrato.
        float wet = lowPass(wetRaw, bbdY, bbdA);
        compY += compA * (std::fabs(wet) - compY);
        const float comp = 1.0f / (1.0f + 0.65f * compY);
        wet = softClip(wet * comp * (1.04f + 0.15f * d));

        const float wetMix = 0.18f + 0.82f * mix;
        const float dryLevel = 1.0f - 0.44f * wetMix;
        const float wetLevel = 0.22f + 0.58f * wetMix;
        const float y = x * dryLevel + wet * wetLevel;
        return softClip(y * 0.92f) * 0.96f;
    }
};

class ChorusPlugin : public Plugin
{
    ChorusCore left;
    ChorusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    ChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChorusDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Chorus"; }
    const char* getDescription() const override { return "CE-2 style BBD chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('C', 'h', 'o', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kChorusNames[index];
        parameter.symbol = kChorusSymbols[index];
        parameter.ranges.min = kChorusMin[index];
        parameter.ranges.max = kChorusMax[index];
        parameter.ranges.def = kChorusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusPlugin)
};

Plugin* createPlugin()
{
    return new ChorusPlugin();
}

END_NAMESPACE_DISTRHO
