/*
 * Chorus20 - stereo BBD chorus/flanger for Rocksmith's Pedal_Chorus20.
 *
 * Local reference: pedals/chorus 2.0.jpg, labelled "Stereo Chorus Flanger".
 * It shows clocked BBD delay, companding/filtering around the delay line, and
 * stereo wet outputs. Rocksmith exposes Rate, Depth, and Mix only, so this
 * keeps regeneration, tone, and stereo offset as internal voicing.
 */
#include "DistrhoPlugin.hpp"
#include "Chorus20Params.h"
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

class Chorus20Core
{
    float sampleRate = 48000.0f;
    float rate = kChorus20Def[kRate];
    float depth = kChorus20Def[kDepth];
    float mix = kChorus20Def[kMix];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY = 0.0f;
    float combY = 0.0f;
    float compY = 0.0f;
    float feedback = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float combA = 0.0f;
    float compA = 0.0f;

    float currentRateHz() const
    {
        const float r = clamp01(rate);
        return 0.055f + 5.20f * std::pow(r, 1.45f);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 32.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        preA = onePoleCoeffHz(7600.0f - 1500.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(4550.0f - 1250.0f * d, sampleRate);
        combA = onePoleCoeffHz(5200.0f - 950.0f * d, sampleRate);
        compA = onePoleCoeffHz(20.0f, sampleRate);
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
        const float s = std::sin(kTwoPi * phase);
        const float second = std::sin(kTwoPi * (phase * 2.0f + 0.17f));
        return 0.86f * s + 0.14f * second;
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
        hpX1 = hpY1 = preY = bbdY = combY = compY = feedback = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.085f));
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

        const float d = 0.06f + 0.94f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 0.92f * mix);

        const float lfo = lfoAt(lfoPhase);
        const float lfoSkew = 0.72f * lfo + 0.28f * std::sin(kTwoPi * (lfoPhase + phaseOffset + 0.25f));

        const float chorusBaseMs = 8.8f + 2.2f * (1.0f - d);
        const float chorusWidthMs = 0.42f + 5.85f * d;
        float chorusMs = chorusBaseMs + chorusWidthMs * lfoSkew;
        chorusMs = std::fmax(2.6f, std::fmin(25.0f, chorusMs));

        const float flangeBaseMs = 2.8f + 1.1f * (1.0f - d);
        const float flangeWidthMs = 0.18f + 1.95f * d;
        float flangeMs = flangeBaseMs + flangeWidthMs * (-0.55f * lfo);
        flangeMs = std::fmax(1.1f, std::fmin(8.5f, flangeMs));

        float x = highPass(in);
        x = lowPass(x, preY, preA);
        x = softClip(x * (1.04f + 0.10f * d)) * 0.96f;

        const float fb = (0.030f + 0.145f * d) * m;
        const float write = softClip(x + feedback * fb);

        const float chorusTap = delay.read(chorusMs * 0.001f * sampleRate);
        const float flangeTap = delay.read(flangeMs * 0.001f * sampleRate);
        delay.write(write);

        float chorusWet = lowPass(chorusTap, bbdY, bbdA);
        float flangeWet = lowPass(flangeTap, combY, combA);
        float wet = chorusWet * (0.93f + 0.16f * d) - flangeWet * (0.12f + 0.23f * d);

        compY += compA * (std::fabs(wet) - compY);
        const float comp = 1.0f / (1.0f + 0.80f * compY);
        wet = softClip(wet * comp * (1.08f + 0.12f * d));
        feedback = wet;

        const float dryLevel = 1.0f - 0.30f * m;
        const float wetLevel = (0.24f + 0.74f * m) * m;
        const float y = in * dryLevel + wet * wetLevel;
        return softClip(y * 0.98f) * 0.99f;
    }
};

class Chorus20Plugin : public Plugin
{
    Chorus20Core left;
    Chorus20Core right;
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
    Chorus20Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChorus20Def[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.42f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Chorus20"; }
    const char* getDescription() const override { return "Stereo BBD chorus flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'h', '2', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kChorus20Names[index];
        parameter.symbol = kChorus20Symbols[index];
        parameter.ranges.min = kChorus20Min[index];
        parameter.ranges.max = kChorus20Max[index];
        parameter.ranges.def = kChorus20Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Chorus20Plugin)
};

Plugin* createPlugin()
{
    return new Chorus20Plugin();
}

END_NAMESPACE_DISTRHO
