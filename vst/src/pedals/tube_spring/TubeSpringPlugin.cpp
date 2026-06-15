/*
 * TubeSpring - two-knob tube/tank spring reverb for the game's
 * Pedal_TubeSpring.
 *
 * Local reference: pedals/spring reverb.gif, a high-voltage spring tank
 * driver/recovery circuit. the game exposes Mix and Depth only, so Depth
 * controls dwell, tank decay, and drip intensity while Mix is the pedal blend.
 * The dry path stays clean; the tube-like compression is applied only to the
 * virtual tank drive so this does not become a hidden boost/distortion pedal.
 */
#include "DistrhoPlugin.hpp"
#include "TubeSpringParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr int kCombCount = 6;
static constexpr int kAllpassCount = 4;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeff(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline int msToSamples(float ms, float sr)
{
    int samples = (int)std::floor(ms * 0.001f * sr + 0.5f);
    return samples < 1 ? 1 : samples;
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
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setBandPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(alpha, 0.0f, -alpha, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
};

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resize(int samples)
    {
        if (samples < 2)
            samples = 2;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(int delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 1)
            return 0.0f;
        if (delaySamples >= size)
            delaySamples = size - 1;
        int index = writeIndex - delaySamples;
        while (index < 0)
            index += size;
        return data[(size_t)index];
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

class AllpassDelay
{
    DelayBuffer buffer;
    int delaySamples = 1;
    float feedback = 0.62f;

public:
    void prepare(float sr, float maxMs)
    {
        buffer.resize(msToSamples(maxMs, sr) + 8);
    }

    void reset()
    {
        buffer.reset();
    }

    void set(float sr, float delayMs, float fb)
    {
        delaySamples = msToSamples(delayMs, sr);
        feedback = fb;
    }

    float process(float x)
    {
        const float delayed = buffer.read(delaySamples);
        const float y = delayed - feedback * x;
        buffer.write(x + feedback * y);
        return y;
    }
};

class DampedComb
{
    DelayBuffer buffer;
    int delaySamples = 1;
    float feedback = 0.78f;
    float damping = 0.35f;
    float filter = 0.0f;

public:
    void prepare(float sr, float maxMs)
    {
        buffer.resize(msToSamples(maxMs, sr) + 8);
    }

    void reset()
    {
        filter = 0.0f;
        buffer.reset();
    }

    void setDelay(float sr, float delayMs)
    {
        delaySamples = msToSamples(delayMs, sr);
    }

    void setDecay(float fb, float damp)
    {
        feedback = fb;
        damping = damp;
    }

    float process(float x)
    {
        const float delayed = buffer.read(delaySamples);
        filter = delayed * (1.0f - damping) + filter * damping;
        buffer.write(x + filter * feedback);
        return delayed;
    }
};

} // namespace

class TubeSpringCore
{
    float sampleRate = 48000.0f;
    float stereoSkew = 1.0f;
    float mix = kTubeSpringDef[kMix];
    float depth = kTubeSpringDef[kDepth];

    Biquad inputHp;
    Biquad inputLp;
    Biquad tankHp;
    Biquad tankLp;
    Biquad dripA;
    Biquad dripB;

    AllpassDelay allpasses[kAllpassCount];
    DampedComb combs[kCombCount];

    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    float fastCoeff = 0.0f;
    float slowCoeff = 0.0f;
    float recoveryY = 0.0f;
    float recoveryA = 0.0f;

    void updateDelayTimes()
    {
        static const float apMs[kAllpassCount] = { 4.6f, 10.7f, 16.9f, 24.8f };
        static const float combMs[kCombCount] = { 25.7f, 32.9f, 40.1f, 49.9f, 58.7f, 70.3f };

        const float d = smoothstep(depth);
        const float stretch = 0.88f + 0.24f * d;
        const float apFeedback = 0.56f + 0.15f * d;
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].set(sampleRate, apMs[i] * stretch * stereoSkew, apFeedback);

        for (int i = 0; i < kCombCount; ++i)
            combs[i].setDelay(sampleRate, combMs[i] * stretch * stereoSkew);
    }

    void updateFilters()
    {
        const float d = smoothstep(depth);
        inputHp.setHighPass(sampleRate, 92.0f, 0.68f);
        inputLp.setLowPass(sampleRate, 6250.0f - 900.0f * d, 0.72f);
        tankHp.setHighPass(sampleRate, 145.0f + 55.0f * (1.0f - d), 0.70f);
        tankLp.setLowPass(sampleRate, 3150.0f + 1350.0f * d, 0.63f);
        dripA.setBandPass(sampleRate, 1700.0f + 430.0f * d, 4.8f + 5.0f * d);
        dripB.setBandPass(sampleRate, 3050.0f + 680.0f * d, 4.1f + 3.8f * d);

        fastCoeff = onePoleCoeff(230.0f, sampleRate);
        slowCoeff = onePoleCoeff(16.0f, sampleRate);
        recoveryA = onePoleCoeff(5600.0f - 1100.0f * d, sampleRate);

        const float feedback = 0.66f + 0.24f * d;
        const float damping = 0.52f - 0.20f * d;
        for (int i = 0; i < kCombCount; ++i)
        {
            const float offset = 1.0f - 0.020f * (float)i;
            combs[i].setDecay(feedback * offset, damping);
        }
    }

public:
    void setStereoSkew(float skew)
    {
        stereoSkew = skew;
        updateDelayTimes();
    }

    void reset()
    {
        fastEnv = slowEnv = recoveryY = 0.0f;
        inputHp.reset();
        inputLp.reset();
        tankHp.reset();
        tankLp.reset();
        dripA.reset();
        dripB.reset();
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].reset();
        for (int i = 0; i < kCombCount; ++i)
            combs[i].reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].prepare(sampleRate, 40.0f);
        for (int i = 0; i < kCombCount; ++i)
            combs[i].prepare(sampleRate, 115.0f);
        updateDelayTimes();
        updateFilters();
        reset();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateDelayTimes();
        updateFilters();
    }

    float process(float in)
    {
        const float dry = in;
        const float d = smoothstep(depth);

        float x = inputHp.process(in);
        x = inputLp.process(x);

        const float absX = std::fabs(x);
        fastEnv += fastCoeff * (absX - fastEnv);
        slowEnv += slowCoeff * (absX - slowEnv);
        const float transient = fastEnv > slowEnv ? fastEnv - slowEnv : 0.0f;
        const float transientSigned = x >= 0.0f ? transient : -transient;

        const float dwell = 0.62f + 1.18f * d;
        float driver = std::tanh(x * dwell) * (0.44f + 0.16f * d);
        const float drip = (dripA.process(transientSigned) + 0.62f * dripB.process(transientSigned))
            * (0.35f + 1.55f * d);
        float tankIn = driver * (0.54f + 0.74f * d) + drip;

        for (int i = 0; i < 2; ++i)
            tankIn = allpasses[i].process(tankIn);

        float tank = 0.0f;
        tank += combs[0].process(tankIn) * 0.88f;
        tank += combs[1].process(tankIn) * -0.70f;
        tank += combs[2].process(tankIn) * 0.76f;
        tank += combs[3].process(tankIn) * -0.62f;
        tank += combs[4].process(tankIn) * 0.54f;
        tank += combs[5].process(tankIn) * -0.48f;
        tank *= 0.30f;

        for (int i = 2; i < kAllpassCount; ++i)
            tank = allpasses[i].process(tank);

        float wet = tankHp.process(tank);
        wet = tankLp.process(wet);
        wet += drip * (0.020f + 0.034f * d);

        recoveryY += recoveryA * (wet - recoveryY);
        wet = recoveryY + (wet - recoveryY) * (0.10f + 0.15f * d);

        const float dryLevel = 1.0f - 0.20f * mix;
        const float wetLevel = mix * (0.46f + 0.50f * d);
        return (dry * dryLevel + wet * wetLevel) * 0.975f;
    }
};

class TubeSpringPlugin : public Plugin
{
    TubeSpringCore left;
    TubeSpringCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    TubeSpringPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTubeSpringDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        left.setStereoSkew(0.991f);
        right.setStereoSkew(1.023f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TubeSpring"; }
    const char* getDescription() const override { return "Tube spring tank reverb"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'b', 'S', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTubeSpringNames[index];
        parameter.symbol = kTubeSpringSymbols[index];
        parameter.ranges.min = kTubeSpringMin[index];
        parameter.ranges.max = kTubeSpringMax[index];
        parameter.ranges.def = kTubeSpringDef[index];
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
        left.setStereoSkew(0.991f);
        right.setStereoSkew(1.023f);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TubeSpringPlugin)
};

Plugin* createPlugin()
{
    return new TubeSpringPlugin();
}

END_NAMESPACE_DISTRHO
