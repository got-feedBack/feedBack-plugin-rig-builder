/*
 * MarshallSupervibe - SV-1/MN3007 BBD vibe for Rocksmith's
 * Pedal_MarshallSupervibe.
 *
 * Local reference: pedals/marshall super vibe.pdf. The schematic shows TL072
 * input/mix stages, an MN3007 BBD clocked by MN3101, direct and delay paths,
 * and control-board pots for sweep/rate, depth, tone and wave-like shaping.
 * Rocksmith exposes Rate, Depth, Mix and Wave.
 */
#include "DistrhoPlugin.hpp"
#include "MarshallSupervibeParams.h"
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

class FirstOrderAllpass
{
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    float process(float x, float sr, float hz)
    {
        hz = std::fmax(20.0f, std::fmin(hz, sr * 0.45f));
        const float t = std::tan(kPi * hz / sr);
        const float a = (1.0f - t) / (1.0f + t);
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class MarshallSupervibeCore
{
    float sampleRate = 48000.0f;
    float rate = kMarshallSupervibeDef[kRate];
    float depth = kMarshallSupervibeDef[kDepth];
    float mix = kMarshallSupervibeDef[kMix];
    float wave = kMarshallSupervibeDef[kWave];
    float phaseOffset = 0.0f;

    DelayBuffer delay;
    FirstOrderAllpass phaseStages[2];

    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float inputY = 0.0f;
    float bbdY = 0.0f;
    float airY = 0.0f;
    float compY = 0.0f;
    float feedback = 0.0f;
    float dc = 0.0f;

    float hpA = 0.0f;
    float inputA = 0.0f;
    float bbdA = 0.0f;
    float airA = 0.0f;
    float compA = 0.0f;

    float currentRateHz() const
    {
        const float r = clamp01(rate);
        return 0.10f + 6.10f * std::pow(r, 1.46f);
    }

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 30.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        const float w = smoothstep(wave);
        inputA = onePoleCoeffHz(7200.0f - 1100.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(3900.0f + 1100.0f * w - 1150.0f * d, sampleRate);
        airA = onePoleCoeffHz(2100.0f + 2400.0f * w, sampleRate);
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

        const float sine = std::sin(kTwoPi * phase);
        const float triangle = 1.0f - 4.0f * std::fabs(phase - 0.5f);
        const float skewPhase = phase < 0.63f ? phase / 0.63f : (1.0f - phase) / 0.37f;
        const float skew = 2.0f * smoothstep(skewPhase) - 1.0f;
        const float w = smoothstep(wave);

        if (w < 0.55f)
        {
            const float a = w / 0.55f;
            return sine * (1.0f - a) + triangle * a;
        }

        const float a = (w - 0.55f) / 0.45f;
        return triangle * (1.0f - a) + skew * a;
    }

public:
    void setPhaseOffset(float v)
    {
        phaseOffset = v - std::floor(v);
    }

    void reset()
    {
        delay.reset();
        for (int i = 0; i < 2; ++i)
            phaseStages[i].reset();
        lfoPhase = phaseOffset;
        hpX1 = hpY1 = inputY = bbdY = airY = compY = feedback = dc = 0.0f;
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

    void setWave(float v)
    {
        wave = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.06f + 0.94f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.06f + 0.98f * mix);
        const float w = smoothstep(wave);

        const float lfo = lfoAt(lfoPhase);
        const float wobble = 0.86f * lfo + 0.14f * std::sin(kTwoPi * (lfoPhase * 2.0f + 0.18f));

        float x = highPass(in);
        x = lowPass(x, inputY, inputA);
        x = softClip(x * (1.025f + 0.075f * d)) * 0.965f;

        const float baseMs = 5.2f + 4.5f * (1.0f - w);
        const float widthMs = 0.42f + 6.70f * d;
        float delayMs = baseMs + widthMs * wobble;
        delayMs = std::fmax(1.55f, std::fmin(24.0f, delayMs));

        const float fb = (0.018f + 0.095f * d + 0.035f * w) * m;
        const float write = softClip(x + feedback * fb);
        float wet = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(write);

        wet = lowPass(wet, bbdY, bbdA);

        static const float phaseBase[2] = { 220.0f, 920.0f };
        const float phaseSweep = 0.55f + 4.8f * (0.5f + 0.5f * wobble) * d;
        wet = phaseStages[0].process(wet, sampleRate, phaseBase[0] * phaseSweep);
        wet = phaseStages[1].process(wet, sampleRate, phaseBase[1] * (0.65f + phaseSweep));

        compY += compA * (std::fabs(wet) - compY);
        const float comp = 1.0f / (1.0f + 0.66f * compY);
        wet = softClip(wet * comp * (1.05f + 0.11f * d));

        const float airBase = lowPass(wet, airY, airA);
        wet = airBase + (wet - airBase) * (0.10f + 0.34f * w);

        dc += 0.00035f * (wet - dc);
        wet -= dc;
        feedback = wet;

        const float throb = 1.0f - (0.055f + 0.125f * d) * (0.5f + 0.5f * wobble);
        wet *= throb;

        const float dryLevel = 1.0f - 0.50f * m;
        const float wetLevel = (0.28f + 0.86f * m) * m;
        float y = x * dryLevel - wet * wetLevel;
        return softClip(y * 0.98f) * 0.97f;
    }
};

class MarshallSupervibePlugin : public Plugin
{
    MarshallSupervibeCore left;
    MarshallSupervibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setWave(params[kWave]);
        right.setWave(params[kWave]);
    }

public:
    MarshallSupervibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMarshallSupervibeDef[i];
        // Keep phase linked; previous vibe tests showed auto-pan is not the
        // desired Rocksmith behavior here.
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarshallSupervibe"; }
    const char* getDescription() const override { return "Marshall SV-1 style BBD vibe"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'S', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarshallSupervibeNames[index];
        parameter.symbol = kMarshallSupervibeSymbols[index];
        parameter.ranges.min = kMarshallSupervibeMin[index];
        parameter.ranges.max = kMarshallSupervibeMax[index];
        parameter.ranges.def = kMarshallSupervibeDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarshallSupervibePlugin)
};

Plugin* createPlugin()
{
    return new MarshallSupervibePlugin();
}

END_NAMESPACE_DISTRHO
