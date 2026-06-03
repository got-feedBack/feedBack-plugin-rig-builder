/*
 * DigitalVerb - early digital reverb for Rocksmith's Pedal_DigitalVerb.
 *
 * Local reference: pedals/digital verb.png, a Boss RV-2 schematic dated 1987.
 * The analog board shows pre/de-emphasis around a digital reverb board. The
 * useful Rocksmith surface is Time, Mix, Depth and Tone, so this models an
 * RV-2-like clean algorithmic verb: short pre-delay, serial diffusion,
 * parallel damped delay tanks, and tone-controlled digital damping.
 */
#include "DistrhoPlugin.hpp"
#include "DigitalVerbParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr int kAllpassCount = 5;
static constexpr int kCombCount = 8;
static constexpr int kEarlyCount = 5;

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

class TapDelay
{
    DelayBuffer buffer;
    int delaySamples = 1;
    float gain = 0.0f;

public:
    void prepare(float sr, float maxMs)
    {
        buffer.resize(msToSamples(maxMs, sr) + 8);
    }

    void reset()
    {
        buffer.reset();
    }

    void set(float sr, float delayMs, float g)
    {
        delaySamples = msToSamples(delayMs, sr);
        gain = g;
    }

    float process(float x)
    {
        const float y = buffer.read(delaySamples) * gain;
        buffer.write(x);
        return y;
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

class DigitalVerbCore
{
    float sampleRate = 48000.0f;
    float stereoSkew = 1.0f;
    float time = kDigitalVerbDef[kTime];
    float mix = kDigitalVerbDef[kMix];
    float depth = kDigitalVerbDef[kDepth];
    float tone = kDigitalVerbDef[kTone];

    Biquad inputHp;
    Biquad inputLp;
    Biquad tankHp;
    Biquad tankLp;
    Biquad airLp;

    TapDelay preDelay;
    TapDelay early[kEarlyCount];
    AllpassDelay allpasses[kAllpassCount];
    DampedComb combs[kCombCount];

    float toneTilt = 0.0f;
    float toneCoeff = 0.0f;

    void updateDelayTimes()
    {
        static const float earlyMs[kEarlyCount] = { 9.7f, 15.3f, 22.9f, 31.1f, 41.5f };
        static const float apMs[kAllpassCount] = { 4.8f, 8.9f, 13.7f, 21.1f, 34.3f };
        static const float combMs[kCombCount] = { 53.9f, 61.7f, 70.1f, 79.3f, 88.7f, 97.9f, 111.7f, 126.1f };

        const float d = smoothstep(depth);
        const float t = smoothstep(time);
        const float size = 0.72f + 0.90f * d + 0.22f * t;
        const float preMs = 4.0f + 34.0f * d + 16.0f * t;
        preDelay.set(sampleRate, preMs * stereoSkew, 1.0f);

        for (int i = 0; i < kEarlyCount; ++i)
        {
            const float sign = (i & 1) ? -1.0f : 1.0f;
            early[i].set(sampleRate, earlyMs[i] * (0.72f + 0.80f * d) * stereoSkew,
                sign * (0.115f - 0.010f * (float)i));
        }

        const float apFeedback = 0.55f + 0.20f * d;
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].set(sampleRate, apMs[i] * (0.85f + 0.55f * d) * stereoSkew, apFeedback);

        for (int i = 0; i < kCombCount; ++i)
            combs[i].setDelay(sampleRate, combMs[i] * size * stereoSkew);
    }

    void updateFilters()
    {
        const float t = smoothstep(time);
        const float d = smoothstep(depth);
        const float bright = smoothstep(tone);

        inputHp.setHighPass(sampleRate, 70.0f + 55.0f * (1.0f - d), 0.70f);
        inputLp.setLowPass(sampleRate, 5200.0f + 7200.0f * bright, 0.70f);
        tankHp.setHighPass(sampleRate, 95.0f + 90.0f * (1.0f - d), 0.70f);
        tankLp.setLowPass(sampleRate, 3100.0f + 8800.0f * bright, 0.62f);
        airLp.setLowPass(sampleRate, 4200.0f + 6500.0f * bright, 0.78f);
        toneCoeff = onePoleCoeff(1050.0f + 2600.0f * bright, sampleRate);

        const float feedback = std::fmin(0.935f, 0.54f + 0.36f * t + 0.035f * d);
        const float damping = std::fmax(0.08f, std::fmin(0.80f, 0.62f - 0.44f * bright + 0.10f * t));
        for (int i = 0; i < kCombCount; ++i)
        {
            const float offset = 1.0f - 0.014f * (float)i;
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
        toneTilt = 0.0f;
        inputHp.reset();
        inputLp.reset();
        tankHp.reset();
        tankLp.reset();
        airLp.reset();
        preDelay.reset();
        for (int i = 0; i < kEarlyCount; ++i)
            early[i].reset();
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].reset();
        for (int i = 0; i < kCombCount; ++i)
            combs[i].reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        preDelay.prepare(sampleRate, 90.0f);
        for (int i = 0; i < kEarlyCount; ++i)
            early[i].prepare(sampleRate, 90.0f);
        for (int i = 0; i < kAllpassCount; ++i)
            allpasses[i].prepare(sampleRate, 70.0f);
        for (int i = 0; i < kCombCount; ++i)
            combs[i].prepare(sampleRate, 260.0f);
        updateDelayTimes();
        updateFilters();
        reset();
    }

    void setTime(float v)
    {
        time = clamp01(v);
        updateDelayTimes();
        updateFilters();
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

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float dry = in;
        const float d = smoothstep(depth);
        const float t = smoothstep(time);
        const float bright = smoothstep(tone);

        float x = inputHp.process(in);
        x = inputLp.process(x);

        float seeded = preDelay.process(x);
        float earlySum = 0.0f;
        for (int i = 0; i < kEarlyCount; ++i)
            earlySum += early[i].process(x);

        float tankIn = seeded * (0.48f + 0.50f * d) + earlySum * (0.46f + 0.24f * d);
        for (int i = 0; i < 3; ++i)
            tankIn = allpasses[i].process(tankIn);

        float tank = 0.0f;
        static const float signs[kCombCount] = { 1.0f, -0.86f, 0.78f, -0.72f, 0.66f, -0.58f, 0.52f, -0.46f };
        for (int i = 0; i < kCombCount; ++i)
            tank += combs[i].process(tankIn) * signs[i];
        tank *= 0.245f + 0.045f * d;

        for (int i = 3; i < kAllpassCount; ++i)
            tank = allpasses[i].process(tank);

        float wet = tankHp.process(tank + earlySum * (0.10f + 0.12f * d));
        wet = tankLp.process(wet);

        toneTilt += toneCoeff * (wet - toneTilt);
        wet = toneTilt + (wet - toneTilt) * (0.08f + 0.42f * bright);
        wet = airLp.process(wet);

        const float density = 0.58f + 0.28f * d + 0.14f * t;
        const float dryLevel = 1.0f - 0.46f * mix;
        const float wetLevel = mix * density;
        const float outTrim = 0.985f - 0.030f * mix;
        return (dry * dryLevel + wet * wetLevel) * outTrim;
    }
};

class DigitalVerbPlugin : public Plugin
{
    DigitalVerbCore left;
    DigitalVerbCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTime(params[kTime]);
        right.setTime(params[kTime]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    DigitalVerbPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDigitalVerbDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        left.setStereoSkew(0.977f);
        right.setStereoSkew(1.031f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DigitalVerb"; }
    const char* getDescription() const override { return "Boss RV-2 style digital reverb"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'g', 'V', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDigitalVerbNames[index];
        parameter.symbol = kDigitalVerbSymbols[index];
        parameter.ranges.min = kDigitalVerbMin[index];
        parameter.ranges.max = kDigitalVerbMax[index];
        parameter.ranges.def = kDigitalVerbDef[index];
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
        left.setStereoSkew(0.977f);
        right.setStereoSkew(1.031f);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalVerbPlugin)
};

Plugin* createPlugin()
{
    return new DigitalVerbPlugin();
}

END_NAMESPACE_DISTRHO
