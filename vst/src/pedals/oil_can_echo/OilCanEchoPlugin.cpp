/*
 * OilCanEcho - Tel-Ray / OK Pacemaker oil-can echo for Rocksmith
 * Pedal_OilCanEcho.
 *
 * Local references: pedals/oilcan_1.png and oilcan_2.jpg. The schematics show
 * the old electrostatic oil-can delay style: discrete transistor preamp,
 * rotating storage can, bias oscillator/power section, and a simple wet/dry
 * output mixer. Rocksmith exposes Time, Feedback, and Mix, so the mechanical
 * modulation, loss, and smear are fixed inside the model.
 */
#include "DistrhoPlugin.hpp"
#include "OilCanEchoParams.h"
#include <cmath>
#include <cstdint>
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
        z1 = 0.0f;
        z2 = 0.0f;
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
        hz = std::fmax(8.0f, std::fmin(hz, sr * 0.45f));
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = std::fmax(8.0f, std::fmin(hz, sr * 0.45f));
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
        const int i2 = (i1 + 1) % size;
        const int im1 = (i0 + size - 1) % size;
        const float frac = pos - (float)i0;
        const float y0 = data[(size_t)im1];
        const float y1 = data[(size_t)i0];
        const float y2 = data[(size_t)i1];
        const float y3 = data[(size_t)i2];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
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

class OilCanEchoCore
{
    float sampleRate = 48000.0f;
    float time = kOilCanEchoDef[kTime];
    float feedback = kOilCanEchoDef[kFeedback];
    float mix = kOilCanEchoDef[kMix];

    DelayBuffer canL;
    DelayBuffer canR;
    Biquad inHpL;
    Biquad inHpR;
    Biquad inLpL;
    Biquad inLpR;
    Biquad loopHpL;
    Biquad loopHpR;
    Biquad loopLp1L;
    Biquad loopLp1R;
    Biquad loopLp2L;
    Biquad loopLp2R;
    Biquad wetLpL;
    Biquad wetLpR;

    float delaySmoothL = 260.0f;
    float delaySmoothR = 272.0f;
    float canMemoryL = 0.0f;
    float canMemoryR = 0.0f;
    float smearL = 0.0f;
    float smearR = 0.0f;
    float leakL = 0.0f;
    float leakR = 0.0f;
    float motorPhase = 0.0f;
    float wobblePhase = 0.0f;
    float ripplePhase = 0.0f;
    uint32_t noiseState = 0x4c11db7u;

    float currentDelayMs() const
    {
        // Existing Rocksmith mapping stores Time as milliseconds / 2000.
        const float ms = time * 2000.0f;
        return std::fmax(80.0f, std::fmin(ms, 620.0f));
    }

    float noise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01(delayMs / 620.0f);
        const float fb = smoothstep(feedback);

        inHpL.setHighPass(sampleRate, 38.0f, 0.68f);
        inHpR.setHighPass(sampleRate, 38.0f, 0.68f);
        inLpL.setLowPass(sampleRate, 5600.0f - 1200.0f * t, 0.62f);
        inLpR.setLowPass(sampleRate, 5450.0f - 1100.0f * t, 0.62f);
        loopHpL.setHighPass(sampleRate, 120.0f + 80.0f * fb, 0.55f);
        loopHpR.setHighPass(sampleRate, 130.0f + 76.0f * fb, 0.55f);
        loopLp1L.setLowPass(sampleRate, 3100.0f - 760.0f * t - 650.0f * fb, 0.50f);
        loopLp1R.setLowPass(sampleRate, 2950.0f - 720.0f * t - 600.0f * fb, 0.50f);
        loopLp2L.setLowPass(sampleRate, 2400.0f - 520.0f * t - 420.0f * fb, 0.50f);
        loopLp2R.setLowPass(sampleRate, 2300.0f - 500.0f * t - 400.0f * fb, 0.50f);
        wetLpL.setLowPass(sampleRate, 4200.0f - 780.0f * t, 0.56f);
        wetLpR.setLowPass(sampleRate, 4050.0f - 720.0f * t, 0.56f);
    }

    float shapeCan(float x, float& leak, float drive)
    {
        leak += 0.0045f * (noise() - leak);
        const float asymmetric = x + leak * 0.0035f + 0.018f;
        const float ref = std::tanh(0.018f * drive);
        return (std::tanh(asymmetric * drive) - ref) * (0.90f / drive);
    }

    float processSide(float input, DelayBuffer& delay, Biquad& inHp, Biquad& inLp,
                      Biquad& loopHp, Biquad& loopLp1, Biquad& loopLp2, Biquad& wetLp,
                      float& delaySmooth, float& canMemory, float& smear, float& leak,
                      float targetMs, float wobbleMs, float side)
    {
        delaySmooth += 0.0016f * (targetMs - delaySmooth);

        const float mainSamples = std::fmax(3.0f, (delaySmooth + wobbleMs) * 0.001f * sampleRate);
        const float earlySamples = std::fmax(3.0f, (delaySmooth * 0.72f + wobbleMs * 0.43f + side * 1.7f) * 0.001f * sampleRate);
        const float lateSamples = std::fmax(3.0f, (delaySmooth * 1.11f - wobbleMs * 0.27f - side * 2.1f) * 0.001f * sampleRate);

        const float main = delay.read(mainSamples);
        const float early = delay.read(earlySamples);
        const float late = delay.read(lateSamples);
        float wet = main * 0.72f + early * 0.17f + late * 0.11f;
        wet = wetLp.process(wet);

        const float t = clamp01(delaySmooth / 620.0f);
        const float fb = 0.12f + feedback * 0.70f;
        const float inputTone = inLp.process(inHp.process(input));

        smear += (0.018f + 0.030f * t) * (wet - smear);
        float loop = loopHp.process(wet * 0.82f + smear * 0.34f + canMemory * 0.18f);
        loop = loopLp1.process(loop);
        loop = loopLp2.process(loop);
        loop = shapeCan(loop, leak, 1.18f + 0.42f * feedback + 0.22f * t);
        canMemory = loop;

        const float write = inputTone + loop * fb;
        delay.write(shapeCan(write, leak, 1.04f + 0.12f * feedback) * 0.98f);
        return wet;
    }

public:
    void reset()
    {
        canL.reset();
        canR.reset();
        inHpL.reset();
        inHpR.reset();
        inLpL.reset();
        inLpR.reset();
        loopHpL.reset();
        loopHpR.reset();
        loopLp1L.reset();
        loopLp1R.reset();
        loopLp2L.reset();
        loopLp2R.reset();
        wetLpL.reset();
        wetLpR.reset();
        delaySmoothL = currentDelayMs();
        delaySmoothR = currentDelayMs() * 1.045f;
        canMemoryL = canMemoryR = smearL = smearR = leakL = leakR = 0.0f;
        motorPhase = wobblePhase = ripplePhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        canL.resize((int)(sampleRate * 0.72f) + 64);
        canR.resize((int)(sampleRate * 0.76f) + 64);
        reset();
    }

    void setTime(float v)
    {
        time = clamp01(v);
        updateFilters();
    }

    void setFeedback(float v)
    {
        feedback = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        const float baseMs = currentDelayMs();
        const float t = clamp01(baseMs / 620.0f);

        motorPhase += (2.0f * kPi * (0.92f + 0.16f * t)) / sampleRate;
        wobblePhase += (2.0f * kPi * (0.13f + 0.05f * feedback)) / sampleRate;
        ripplePhase += (2.0f * kPi * (7.3f + 1.4f * t)) / sampleRate;
        if (motorPhase >= 2.0f * kPi)
            motorPhase -= 2.0f * kPi;
        if (wobblePhase >= 2.0f * kPi)
            wobblePhase -= 2.0f * kPi;
        if (ripplePhase >= 2.0f * kPi)
            ripplePhase -= 2.0f * kPi;

        const float mechanical = std::sin(motorPhase) * (2.2f + 2.8f * t)
                               + std::sin(wobblePhase) * (4.6f + 3.4f * t)
                               + std::sin(ripplePhase) * (0.22f + 0.28f * t);

        const float mono = (inL + inR) * 0.5f;
        const float feedL = mono * 0.70f + inL * 0.30f;
        const float feedR = mono * 0.68f + inR * 0.32f;

        const float wetL = processSide(feedL, canL, inHpL, inLpL, loopHpL, loopLp1L, loopLp2L, wetLpL,
                                       delaySmoothL, canMemoryL, smearL, leakL, baseMs, mechanical, -1.0f);
        const float wetR = processSide(feedR, canR, inHpR, inLpR, loopHpR, loopLp1R, loopLp2R, wetLpR,
                                       delaySmoothR, canMemoryR, smearR, leakR, baseMs * 1.045f, -mechanical * 0.66f, 1.0f);

        const float wetGain = mix * (0.74f + 0.10f * feedback);
        const float dryGain = 1.0f - 0.30f * mix;
        outL = inL * dryGain + wetL * wetGain;
        outR = inR * dryGain + wetR * wetGain;
    }
};

class OilCanEchoPlugin : public Plugin
{
    OilCanEchoCore core;
    float params[kParamCount];

    void recalc()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    OilCanEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOilCanEchoDef[i];
        core.setSampleRate((float)getSampleRate());
        recalc();
    }

protected:
    const char* getLabel() const override { return "OilCanEcho"; }
    const char* getDescription() const override { return "Oil can echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'i', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOilCanEchoNames[index];
        parameter.symbol = kOilCanEchoSymbols[index];
        parameter.ranges.min = kOilCanEchoMin[index];
        parameter.ranges.max = kOilCanEchoMax[index];
        parameter.ranges.def = kOilCanEchoDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < (uint32_t)kParamCount)
        {
            params[index] = value;
            recalc();
        }
    }

    void sampleRateChanged(double sampleRate) override
    {
        core.setSampleRate((float)sampleRate);
        recalc();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
            core.process(inL[i], inR[i], outL[i], outR[i]);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OilCanEchoPlugin)
};

Plugin* createPlugin()
{
    return new OilCanEchoPlugin();
}

END_NAMESPACE_DISTRHO
