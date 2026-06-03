/*
 * VintageFlanger - Deluxe Electric Mistress style BBD flanger for
 * Rocksmith's Pedal_VintageFlanger.
 *
 * Local reference: pedals/vintage flanger.jpg. The schematic shows RD5106A
 * BBD delay, LM324 triangle LFO/range control, and a fixed Color feedback
 * path. Rocksmith exposes only Rate, Depth, and Mix, so Color is voiced
 * internally.
 */
#include "DistrhoPlugin.hpp"
#include "VintageFlangerParams.h"
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

class VintageFlangerCore
{
    float sampleRate = 48000.0f;
    float rate = kVintageFlangerDef[kRate];
    float depth = kVintageFlangerDef[kDepth];
    float mix = kVintageFlangerDef[kMix];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY1 = 0.0f;
    float bbdY2 = 0.0f;
    float colorY = 0.0f;
    float fbState = 0.0f;
    float clockBleedPhase = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float colorA = 0.0f;

    float currentRateHz() const
    {
        return 0.045f + 3.35f * std::pow(clamp01(rate), 1.35f);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 30.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        preA = onePoleCoeffHz(6900.0f - 750.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(5200.0f - 1200.0f * d, sampleRate);
        colorA = onePoleCoeffHz(2450.0f, sampleRate);
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

    float triangle(float phase) const
    {
        phase -= std::floor(phase);
        return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
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
        hpX1 = hpY1 = preY = bbdY1 = bbdY2 = colorY = fbState = 0.0f;
        clockBleedPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.055f));
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
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.025f + 0.975f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.06f + 1.08f * mix);

        float x = highPass(in);
        x = lowPass(x, preY, preA);
        x = softClip(x * 1.03f) * 0.96f;

        const float tri = triangle(lfoPhase + phaseOffset);
        const float shaped = 0.5f + 0.5f * (0.78f * tri + 0.22f * std::sin(kTwoPi * (lfoPhase + phaseOffset)));
        const float baseMs = 0.82f + 2.45f * (1.0f - d);
        const float rangeMs = 0.46f + 8.85f * d;
        float delayMs = baseMs + rangeMs * shaped;
        delayMs = std::fmax(0.48f, std::fmin(13.5f, delayMs));

        // Deluxe Electric Mistress Color is not a Rocksmith knob here. Keep a
        // fixed moderate negative feedback so low Mix presets still flange.
        const float color = 0.24f + 0.22f * d + 0.16f * m;
        const float write = softClip(x - fbState * color);
        const float tap = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(write);

        float wet = lowPass(tap, bbdY1, bbdA);
        wet = lowPass(wet, bbdY2, bbdA);
        const float colorBand = wet - lowPass(wet, colorY, colorA);
        wet = softClip(wet + 0.22f * colorBand);
        fbState = wet;

        clockBleedPhase += (9200.0f + 6200.0f * shaped) / sampleRate;
        if (clockBleedPhase >= 1.0f)
            clockBleedPhase -= std::floor(clockBleedPhase);
        const float clockBleed = 0.0011f * d * std::sin(kTwoPi * clockBleedPhase);

        const float dryLevel = 1.0f - 0.24f * m;
        const float wetLevel = (0.30f + 0.92f * m) * m;
        const float y = x * dryLevel - wet * wetLevel + clockBleed;
        return softClip(y * 0.94f) * 0.97f;
    }
};

class VintageFlangerPlugin : public Plugin
{
    VintageFlangerCore left;
    VintageFlangerCore right;
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
    VintageFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kVintageFlangerDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.07f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "VintageFlanger"; }
    const char* getDescription() const override { return "Deluxe Electric Mistress style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 't', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kVintageFlangerNames[index];
        parameter.symbol = kVintageFlangerSymbols[index];
        parameter.ranges.min = kVintageFlangerMin[index];
        parameter.ranges.max = kVintageFlangerMax[index];
        parameter.ranges.def = kVintageFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageFlangerPlugin)
};

Plugin* createPlugin()
{
    return new VintageFlangerPlugin();
}

END_NAMESPACE_DISTRHO
