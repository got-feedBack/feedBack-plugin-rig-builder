/*
 * DigitalChorus - clean digital chorus for Rocksmith's Pedal_DigitalChorus.
 *
 * Local reference: pedals/digital chorus.png. The circuit uses an ESS digital
 * delay core with separate rate/depth modulation, low/high filter networks,
 * and an effect level control. Rocksmith exposes Rate, Depth, LoFilter,
 * HiFilter, and Mix.
 */
#include "DistrhoPlugin.hpp"
#include "DigitalChorusParams.h"
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

class DigitalChorusCore
{
    float sampleRate = 48000.0f;
    float rate = kDigitalChorusDef[kRate];
    float depth = kDigitalChorusDef[kDepth];
    float loFilter = kDigitalChorusDef[kLoFilter];
    float hiFilter = kDigitalChorusDef[kHiFilter];
    float mix = kDigitalChorusDef[kMix];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float inputY = 0.0f;
    float wetY = 0.0f;
    float preEmphasisY = 0.0f;

    float hpA = 0.0f;
    float inputA = 0.0f;
    float wetA = 0.0f;
    float preA = 0.0f;

    float currentRateHz() const
    {
        return 0.075f + 6.30f * std::pow(clamp01(rate), 1.38f);
    }

    void updateFilters()
    {
        const float lo = smoothstep(loFilter);
        const float hi = smoothstep(hiFilter);
        const float hpHz = 24.0f + 620.0f * lo;
        const float lpHz = 11800.0f - 8200.0f * hi;

        const float dt = 1.0f / sampleRate;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        inputA = onePoleCoeffHz(13200.0f, sampleRate);
        wetA = onePoleCoeffHz(lpHz, sampleRate);
        preA = onePoleCoeffHz(2800.0f + 3600.0f * (1.0f - hi), sampleRate);
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
        const float tri = 4.0f * std::fabs(phase - 0.5f) - 1.0f;
        const float sine = std::sin(kTwoPi * phase);
        return 0.62f * sine - 0.38f * tri;
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
        hpX1 = hpY1 = inputY = wetY = preEmphasisY = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.070f));
        reset();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
    }

    void setLoFilter(float v)
    {
        loFilter = clamp01(v);
        updateFilters();
    }

    void setHiFilter(float v)
    {
        hiFilter = clamp01(v);
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

        const float d = 0.05f + 0.95f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.10f + 0.96f * mix);

        float x = highPass(in);
        x = lowPass(x, inputY, inputA);
        preEmphasisY += preA * (x - preEmphasisY);
        x = 0.88f * x + 0.12f * (x - preEmphasisY);

        const float wobble = lfoAt(lfoPhase);
        const float wobble2 = lfoAt(lfoPhase + 0.31f);
        const float baseMs = 13.8f + 3.2f * (1.0f - d);
        const float widthMs = 0.35f + 8.4f * d;
        float delayA = baseMs + widthMs * wobble;
        float delayB = baseMs * 1.42f + widthMs * 0.62f * wobble2;
        delayA = std::fmax(3.0f, std::fmin(34.0f, delayA));
        delayB = std::fmax(4.5f, std::fmin(42.0f, delayB));

        const float tapA = delay.read(delayA * 0.001f * sampleRate);
        const float tapB = delay.read(delayB * 0.001f * sampleRate);
        delay.write(x);

        float wet = 0.68f * tapA + 0.32f * tapB;
        wet = lowPass(wet, wetY, wetA);
        wet = softClip(wet * (1.01f + 0.035f * d));

        const float dryLevel = 1.0f - 0.30f * m;
        const float wetLevel = (0.34f + 0.74f * m) * m;
        const float y = in * dryLevel + wet * wetLevel;
        return softClip(y * 0.93f) * 0.98f;
    }
};

class DigitalChorusPlugin : public Plugin
{
    DigitalChorusCore left;
    DigitalChorusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setLoFilter(params[kLoFilter]);
        right.setLoFilter(params[kLoFilter]);
        left.setHiFilter(params[kHiFilter]);
        right.setHiFilter(params[kHiFilter]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    DigitalChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDigitalChorusDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.50f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DigitalChorus"; }
    const char* getDescription() const override { return "Clean digital chorus with low/high filters"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'g', 'C', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDigitalChorusNames[index];
        parameter.symbol = kDigitalChorusSymbols[index];
        parameter.ranges.min = kDigitalChorusMin[index];
        parameter.ranges.max = kDigitalChorusMax[index];
        parameter.ranges.def = kDigitalChorusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalChorusPlugin)
};

Plugin* createPlugin()
{
    return new DigitalChorusPlugin();
}

END_NAMESPACE_DISTRHO
