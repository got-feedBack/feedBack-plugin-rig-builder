/*
 * SendInTheClones - Clone Theory style multi-voice BBD chorus/doubler for
 * Rocksmith's Pedal_SendInTheClones.
 *
 * Local reference: pedals/send in the clones.png, an Electro-Harmonix Clone
 * Theory vero/schematic-derived layout using MN3007/CD4047 BBD modulation.
 * Rocksmith exposes only Clones, Depth, and Mix, so Rate and mode are fixed
 * internally and Clones controls the extra delay voices/spread.
 */
#include "DistrhoPlugin.hpp"
#include "SendInTheClonesParams.h"
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

class CloneTheoryCore
{
    float sampleRate = 48000.0f;
    float clones = kSitcDef[kClones];
    float depth = kSitcDef[kDepth];
    float mix = kSitcDef[kMix];
    float phaseOffset = 0.0f;

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

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 32.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        const float c = cloneModeAmount();
        preA = onePoleCoeffHz(6900.0f - 900.0f * d - 450.0f * c, sampleRate);
        bbdA = onePoleCoeffHz(4100.0f - 850.0f * d - 420.0f * c, sampleRate);
        compA = onePoleCoeffHz(24.0f, sampleRate);
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
        const float s1 = std::sin(phase * 2.0f * kPi);
        const float s2 = std::sin((phase * 2.0f + 0.31f) * 2.0f * kPi);
        return 0.82f * s1 + 0.18f * s2;
    }

    float cloneModeAmount() const
    {
        if (clones <= 0.0001f)
            return 0.0f;
        // Rocksmith sends mode-like low values (1, 11, 35), not a plain
        // wet amount. Start non-zero Clones in Chorus 1 territory.
        return smoothstep(clamp01(0.47f + 1.22f * clones));
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
        delay.resize((int)(sampleRate * 0.090f));
        reset();
    }

    void setPhaseOffset(float v)
    {
        phaseOffset = v - std::floor(v);
    }

    void setClones(float v)
    {
        clones = clamp01(v);
        updateFilters();
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
        const float c = cloneModeAmount();
        const float d = 0.07f + 0.93f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 1.18f * mix);

        const float rateHz = 0.16f + 0.34f * c + 0.08f * d;
        lfoPhase += rateHz / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        float x = highPass(in);
        x = lowPass(x, preY, preA);
        x = softClip(x * (1.04f + 0.10f * c)) * (0.98f - 0.03f * c);

        const float baseDelaysMs[4] = { 8.1f, 12.7f, 18.4f, 25.8f };
        const float voiceWeights[4] = {
            1.0f,
            0.28f + 0.72f * c,
            smoothstep((c - 0.15f) * 1.75f),
            smoothstep((c - 0.42f) * 1.85f),
        };
        const float phaseSteps[4] = { 0.00f, 0.29f, 0.57f, 0.83f };

        const float widthMs = 0.30f + 7.4f * d;
        float wet = 0.0f;
        float wetNorm = 0.0001f;
        for (int i = 0; i < 4; ++i)
        {
            const float w = voiceWeights[i];
            const float wobble = lfoAt(lfoPhase + phaseSteps[i]);
            float delayMs = baseDelaysMs[i] + 2.8f * (float)i * c + widthMs * (0.54f + 0.12f * (float)i) * wobble;
            delayMs = std::fmax(2.0f, std::fmin(48.0f, delayMs));
            wet += delay.read(delayMs * 0.001f * sampleRate) * w;
            wetNorm += w;
        }

        delay.write(x);
        wet /= wetNorm;

        wet = lowPass(wet, bbdY, bbdA);
        compY += compA * (std::fabs(wet) - compY);
        const float comp = 1.0f / (1.0f + 0.80f * compY);
        wet = softClip((wet - 0.055f * c * x) * comp * (1.08f + 0.18f * d));

        const float dryLevel = 1.0f - 0.36f * m;
        const float wetLevel = (0.24f + 0.72f * c) * m;
        const float y = in * dryLevel + wet * wetLevel;
        return softClip(y * (0.94f + 0.03f * c)) * 0.96f;
    }
};

class SendInTheClonesPlugin : public Plugin
{
    CloneTheoryCore left;
    CloneTheoryCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setClones(params[kClones]);
        right.setClones(params[kClones]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    SendInTheClonesPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSitcDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.37f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SendInTheClones"; }
    const char* getDescription() const override { return "Clone Theory style BBD chorus/doubler"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 2); }
    int64_t getUniqueId() const override { return d_cconst('S', 'C', 'l', 'n'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSitcNames[index];
        parameter.symbol = kSitcSymbols[index];
        parameter.ranges.min = kSitcMin[index];
        parameter.ranges.max = kSitcMax[index];
        parameter.ranges.def = kSitcDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SendInTheClonesPlugin)
};

Plugin* createPlugin()
{
    return new SendInTheClonesPlugin();
}

END_NAMESPACE_DISTRHO
