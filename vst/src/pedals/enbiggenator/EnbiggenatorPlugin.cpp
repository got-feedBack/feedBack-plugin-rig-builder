/*
 * Enbiggenator - Rocksmith widening/thickening pedal.
 *
 * No exact schematic is available. The pedal art and knobs point to a
 * modulated thickener: Rate controls a slow LFO, Depth controls detune/delay
 * spread and body color, and Mix blends in the widened doubled path.
 */
#include "DistrhoPlugin.hpp"
#include "EnbiggenatorParams.h"
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
        delaySamples = std::fmax(1.0f, std::fmin(delaySamples, (float)(size - 3)));
        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        const int i0 = (int)std::floor(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }

    void write(float x)
    {
        data[(size_t)writeIndex] = x;
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

} // namespace

class EnbiggenatorCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float rate = kEnbiggenatorDef[kRate];
    float depth = kEnbiggenatorDef[kDepth];
    float mix = kEnbiggenatorDef[kMix];

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float lowBody = 0.0f;
    float air = 0.0f;
    float hpA = 0.0f;
    float bodyA = 0.0f;
    float airA = 0.0f;

    void update()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 32.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        bodyA = 1.0f - std::exp(-2.0f * kPi * (260.0f + 520.0f * d) / sampleRate);
        airA = 1.0f - std::exp(-2.0f * kPi * (4600.0f + 2800.0f * (1.0f - d)) / sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

public:
    void setPhaseOffset(float v)
    {
        phaseOffset = v;
    }

    void reset()
    {
        delay.reset();
        lfoPhase = phaseOffset;
        hpX1 = hpY1 = lowBody = air = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.070f));
        reset();
    }

    void setParams(float r, float d, float m)
    {
        rate = clamp01(r);
        depth = clamp01(d);
        mix = clamp01(m);
        update();
    }

    float process(float in)
    {
        const float rHz = 0.09f + 5.1f * std::pow(rate, 1.45f);
        lfoPhase += rHz / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = smoothstep(depth);
        const float m = smoothstep(mix);
        float x = highPass(in);

        lowBody += bodyA * (x - lowBody);
        air += airA * (x - air);
        const float conditioned = air + lowBody * (0.10f + 0.22f * d);

        const float lfo = std::sin(kTwoPi * lfoPhase);
        const float delayMs = 10.0f + 15.0f * d + lfo * (0.7f + 5.2f * d);
        float wet = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(std::tanh(conditioned * (1.0f + 0.42f * d)));

        const float body = lowBody * (0.24f + 0.34f * d);
        wet = wet * (0.72f + 0.20f * d) + body;
        wet = std::tanh(wet * (1.0f + 0.22f * d));

        const float dryLevel = 1.0f - 0.38f * m;
        const float wetLevel = m * (0.68f + 0.10f * d);
        return (x * dryLevel + wet * wetLevel) * 0.92f;
    }
};

class EnbiggenatorPlugin : public Plugin
{
    EnbiggenatorCore left;
    EnbiggenatorCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setParams(params[kRate], params[kDepth], params[kMix]);
        right.setParams(params[kRate], params[kDepth], params[kMix]);
    }

public:
    EnbiggenatorPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEnbiggenatorDef[i];
        left.setPhaseOffset(0.07f);
        right.setPhaseOffset(0.57f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Enbiggenator"; }
    const char* getDescription() const override { return "modulated thickener and widener"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', 'n', 'B', 'g'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEnbiggenatorNames[index];
        parameter.symbol = kEnbiggenatorSymbols[index];
        parameter.ranges.min = kEnbiggenatorMin[index];
        parameter.ranges.max = kEnbiggenatorMax[index];
        parameter.ranges.def = kEnbiggenatorDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnbiggenatorPlugin)
};

Plugin* createPlugin()
{
    return new EnbiggenatorPlugin();
}

END_NAMESPACE_DISTRHO
