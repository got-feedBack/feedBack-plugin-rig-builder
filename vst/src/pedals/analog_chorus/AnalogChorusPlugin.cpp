/*
 * AnalogChorus - warm stereo BBD chorus for the game's analog/vintage
 * chorus pedal. The local reference shows MN3009-style BBD delay, analog
 * filtering, companding, and dual outputs. the game exposes Rate, Depth,
 * and Mix only, so the multi-voice spread is fixed internally.
 */
#include "DistrhoPlugin.hpp"
#include "AnalogChorusParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;

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

class AnalogChorusCore
{
    float sampleRate = 48000.0f;
    float rate = kAnalogChorusDef[kRate];
    float depth = kAnalogChorusDef[kDepth];
    float mix = kAnalogChorusDef[kMix];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float inputY = 0.0f;
    float bbdY = 0.0f;
    float compY = 0.0f;
    float dc = 0.0f;

    float hpA = 0.0f;
    float inputA = 0.0f;
    float bbdA = 0.0f;
    float compA = 0.0f;

    float currentRateHz() const
    {
        return 0.065f + 3.45f * std::pow(clamp01(rate), 1.28f);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 28.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        inputA = onePoleCoeffHz(6500.0f - 1250.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(4300.0f - 1150.0f * d, sampleRate);
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

    float lfoAt(float phase) const
    {
        phase += phaseOffset;
        phase -= std::floor(phase);
        const float s1 = std::sin(kTwoPi * phase);
        const float s2 = std::sin(kTwoPi * (phase * 0.5f + 0.19f));
        return 0.78f * s1 + 0.22f * s2;
    }

public:
    void setPhaseOffset(float v)
    {
        phaseOffset = v - std::floor(v);
    }

    void reset()
    {
        delay.reset();
        lfoPhase = phaseOffset;
        hpX1 = hpY1 = inputY = bbdY = compY = dc = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.095f));
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
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.08f + 0.92f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 0.96f * mix);

        float x = highPass(in);
        x = lowPass(x, inputY, inputA);
        x = softClip(x * (1.05f + 0.10f * d)) * 0.95f;

        const float baseMs[3] = { 10.8f, 15.7f, 22.4f };
        const float phaseStep[3] = { 0.00f, 0.34f, 0.68f };
        const float weights[3] = { 0.92f, 0.74f, 0.54f };
        const float widthMs = 0.35f + 6.8f * d;

        float wet = 0.0f;
        float norm = 0.0001f;
        for (int i = 0; i < 3; ++i)
        {
            const float wobble = lfoAt(lfoPhase + phaseStep[i]);
            float delayMs = baseMs[i] + widthMs * (0.72f + 0.13f * (float)i) * wobble;
            delayMs = std::fmax(3.5f, std::fmin(44.0f, delayMs));
            wet += delay.read(delayMs * 0.001f * sampleRate) * weights[i];
            norm += weights[i];
        }
        delay.write(x);
        wet /= norm;

        wet = lowPass(wet, bbdY, bbdA);
        compY += compA * (std::fabs(wet) - compY);
        const float comp = 1.0f / (1.0f + 0.72f * compY);
        wet = softClip(wet * comp * (1.08f + 0.14f * d));

        dc += 0.00045f * (wet - dc);
        wet -= dc;

        const float dryLevel = 1.0f - 0.30f * m;
        const float wetLevel = (0.30f + 0.72f * m) * m;
        const float y = in * dryLevel + wet * wetLevel;
        return softClip(y * 0.94f) * 0.97f;
    }
};

class AnalogChorusPlugin : public Plugin
{
    AnalogChorusCore left;
    AnalogChorusCore right;
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
    AnalogChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAnalogChorusDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.37f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AnalogChorus"; }
    const char* getDescription() const override { return "Warm stereo BBD analog chorus"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'n', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAnalogChorusNames[index];
        parameter.symbol = kAnalogChorusSymbols[index];
        parameter.ranges.min = kAnalogChorusMin[index];
        parameter.ranges.max = kAnalogChorusMax[index];
        parameter.ranges.def = kAnalogChorusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogChorusPlugin)
};

Plugin* createPlugin()
{
    return new AnalogChorusPlugin();
}

END_NAMESPACE_DISTRHO
