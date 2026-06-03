/*
 * MultiVibe - Boss VB-2 style BBD vibrato for Rocksmith's Pedal_MultiVibe.
 *
 * Local reference: pedals/multi vibe.jpg. The schematic is a Boss VB-2 with
 * NJM4558 input/output stages, MN3207/MN3102 BBD clock pair, Rate, Depth and
 * Rise Time controls. Rocksmith exposes Speed, Mix and Waveform, so Speed
 * drives the LFO, Mix controls wet/depth intensity, and Waveform shapes the
 * VB-2-style LFO/rise character.
 */
#include "DistrhoPlugin.hpp"
#include "MultiVibeParams.h"
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

class MultiVibeCore
{
    float sampleRate = 48000.0f;
    float speed = kMultiVibeDef[kSpeed];
    float mix = kMultiVibeDef[kMix];
    float waveform = kMultiVibeDef[kWaveform];

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float lfoLag = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY = 0.0f;
    float airY = 0.0f;
    float dc = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float airA = 0.0f;
    float lfoA = 0.0f;

    float rateHz() const
    {
        const float s = clamp01(speed);
        return 0.10f + 7.30f * std::pow(s, 1.38f);
    }

    void updateCoeffs()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 28.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float depth = smoothstep(mix);
        const float wave = smoothstep(waveform);
        preA = onePoleCoeffHz(7600.0f - 1000.0f * depth, sampleRate);
        bbdA = onePoleCoeffHz(4550.0f - 1050.0f * depth + 450.0f * wave, sampleRate);
        airA = onePoleCoeffHz(1900.0f + 2500.0f * wave, sampleRate);
        lfoA = onePoleCoeffHz(18.0f + 16.0f * speed, sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    static float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

    float lfoShape(float phase) const
    {
        phase -= std::floor(phase);

        const float sine = std::sin(kTwoPi * phase);
        const float triangle = 1.0f - 4.0f * std::fabs(phase - 0.5f);
        const float rise = phase < 0.58f ? phase / 0.58f : (1.0f - phase) / 0.42f;
        const float asym = 2.0f * smoothstep(rise) - 1.0f;
        const float w = smoothstep(waveform);

        if (w < 0.55f)
        {
            const float a = w / 0.55f;
            return sine * (1.0f - a) + triangle * a;
        }

        const float a = (w - 0.55f) / 0.45f;
        return triangle * (1.0f - a) + asym * a;
    }

public:
    void reset()
    {
        delay.reset();
        lfoPhase = 0.0f;
        lfoLag = 0.0f;
        hpX1 = hpY1 = preY = bbdY = airY = dc = 0.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.060f));
        reset();
    }

    void setSpeed(float v)
    {
        speed = clamp01(v);
        updateCoeffs();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
        updateCoeffs();
    }

    void setWaveform(float v)
    {
        waveform = clamp01(v);
        updateCoeffs();
    }

    float process(float in)
    {
        lfoPhase += rateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float intensity = smoothstep(mix);
        const float depth = 0.04f + 0.96f * intensity;
        const float rawLfo = lfoShape(lfoPhase);
        lfoLag += lfoA * (rawLfo - lfoLag);

        float x = highPass(in);
        x = lowPass(x, preY, preA);

        const float baseMs = 6.15f;
        const float widthMs = 0.55f + 5.55f * depth;
        const float delayMs = std::fmax(1.35f, std::fmin(18.0f, baseMs + widthMs * lfoLag));

        float wet = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(x);

        wet = lowPass(wet, bbdY, bbdA);
        const float darker = lowPass(wet, airY, airA);
        wet = darker + (wet - darker) * (0.18f + 0.22f * smoothstep(waveform));

        dc += 0.00035f * (wet - dc);
        wet -= dc;

        const float throb = 1.0f - (0.018f + 0.055f * intensity) * (0.5f + 0.5f * lfoLag);
        wet *= throb;

        const float dryLevel = 1.0f - 0.94f * intensity;
        const float wetLevel = 0.96f * intensity;
        const float y = x * dryLevel + wet * wetLevel;
        return y * 0.985f;
    }
};

class MultiVibePlugin : public Plugin
{
    MultiVibeCore left;
    MultiVibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setWaveform(params[kWaveform]);
        right.setWaveform(params[kWaveform]);
    }

public:
    MultiVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMultiVibeDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MultiVibe"; }
    const char* getDescription() const override { return "Boss VB-2 style BBD vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'l', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMultiVibeNames[index];
        parameter.symbol = kMultiVibeSymbols[index];
        parameter.ranges.min = kMultiVibeMin[index];
        parameter.ranges.max = kMultiVibeMax[index];
        parameter.ranges.def = kMultiVibeDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiVibePlugin)
};

Plugin* createPlugin()
{
    return new MultiVibePlugin();
}

END_NAMESPACE_DISTRHO
