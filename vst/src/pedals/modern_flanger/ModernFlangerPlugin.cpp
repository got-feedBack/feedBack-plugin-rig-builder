/*
 * ModernFlanger - Moog MF-108M/Cluster-Flux style BBD flanger for
 * Rocksmith's Pedal_ModernFlanger.
 *
 * Local reference: pedals/modern flange.pdf. The schematic shows a short BBD
 * delay line, 6th-order low-pass filters, LM13700 direct/delay mix VCAs, and
 * feedback controlled by a CV VCA. Rocksmith exposes Rate, Depth, Regen, Mix.
 */
#include "DistrhoPlugin.hpp"
#include "ModernFlangerParams.h"
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

class ModernFlangerCore
{
    float sampleRate = 48000.0f;
    float rate = kModernFlangerDef[kRate];
    float depth = kModernFlangerDef[kDepth];
    float regen = kModernFlangerDef[kRegen];
    float mix = kModernFlangerDef[kMix];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float inputY1 = 0.0f;
    float inputY2 = 0.0f;
    float bbdY1 = 0.0f;
    float bbdY2 = 0.0f;
    float outputY1 = 0.0f;
    float outputY2 = 0.0f;
    float fbState = 0.0f;
    float compY = 0.0f;

    float hpA = 0.0f;
    float inputA = 0.0f;
    float bbdA = 0.0f;
    float outputA = 0.0f;
    float compA = 0.0f;

    float currentRateHz() const
    {
        return 0.035f + 5.40f * std::pow(clamp01(rate), 1.42f);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        const float r = smoothstep(regen);
        inputA = onePoleCoeffHz(6950.0f - 780.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(6150.0f - 1050.0f * d - 420.0f * r, sampleRate);
        outputA = onePoleCoeffHz(7250.0f - 900.0f * d - 520.0f * r, sampleRate);
        compA = onePoleCoeffHz(28.0f, sampleRate);
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

    float chebyishLowPass(float x, float& z1, float& z2, float a)
    {
        const float y1 = lowPass(x, z1, a);
        return lowPass(y1, z2, a);
    }

    float lfoAt(float phase) const
    {
        phase += phaseOffset;
        phase -= std::floor(phase);
        const float s = std::sin(kTwoPi * phase);
        const float s2 = std::sin(kTwoPi * (phase * 2.0f + 0.11f));
        return 0.86f * s + 0.14f * s2;
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
        hpX1 = hpY1 = inputY1 = inputY2 = bbdY1 = bbdY2 = outputY1 = outputY2 = 0.0f;
        fbState = compY = 0.0f;
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
        updateFilters();
    }

    void setRegen(float v)
    {
        regen = clamp01(v);
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

        const float d = 0.03f + 0.97f * smoothstep(depth);
        const float r = smoothstep(regen);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 0.95f * mix);

        float x = highPass(in);
        x = chebyishLowPass(x, inputY1, inputY2, inputA);
        x = softClip(x * 1.04f) * 0.96f;

        const float lfo = lfoAt(lfoPhase);
        const float lfoUni = 0.5f + 0.5f * lfo;
        const float baseMs = 0.72f + 4.80f * (1.0f - d);
        const float sweepMs = 0.34f + 9.60f * d;
        float mainDelayMs = baseMs + sweepMs * lfoUni;
        mainDelayMs = std::fmax(0.42f, std::fmin(17.5f, mainDelayMs));

        float shortDelayMs = 0.42f + 0.33f * mainDelayMs + 0.55f * d * (1.0f - lfoUni);
        shortDelayMs = std::fmax(0.32f, std::fmin(6.8f, shortDelayMs));

        const float feedbackAmount = (0.025f + 0.74f * r) * (0.74f + 0.26f * m);
        const float feedbackPolarity = (regen < 0.025f) ? 0.0f : -1.0f;
        const float write = softClip(x + feedbackPolarity * fbState * feedbackAmount);

        const float mainTap = delay.read(mainDelayMs * 0.001f * sampleRate);
        const float shortTap = delay.read(shortDelayMs * 0.001f * sampleRate);
        delay.write(write);

        float bbd = chebyishLowPass(0.76f * mainTap + 0.24f * shortTap, bbdY1, bbdY2, bbdA);
        compY += compA * (std::fabs(bbd) - compY);
        const float comp = 1.0f / (1.0f + 0.72f * compY);
        bbd = softClip(bbd * comp * (1.06f + 0.22f * r));
        fbState = bbd;

        float wet = chebyishLowPass(bbd, outputY1, outputY2, outputA);
        wet = softClip(wet * (1.02f + 0.08f * d));

        const float dryLevel = 1.0f - 0.31f * m;
        const float wetLevel = (0.28f + 0.88f * m) * m;
        const float y = x * dryLevel - wet * wetLevel;
        return softClip(y * 0.95f) * 0.97f;
    }
};

class ModernFlangerPlugin : public Plugin
{
    ModernFlangerCore left;
    ModernFlangerCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setRegen(params[kRegen]);
        right.setRegen(params[kRegen]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    ModernFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kModernFlangerDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.50f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ModernFlanger"; }
    const char* getDescription() const override { return "MF-108M style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'd', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kModernFlangerNames[index];
        parameter.symbol = kModernFlangerSymbols[index];
        parameter.ranges.min = kModernFlangerMin[index];
        parameter.ranges.max = kModernFlangerMax[index];
        parameter.ranges.def = kModernFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModernFlangerPlugin)
};

Plugin* createPlugin()
{
    return new ModernFlangerPlugin();
}

END_NAMESPACE_DISTRHO
