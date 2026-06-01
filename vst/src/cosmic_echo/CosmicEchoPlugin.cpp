/*
 * CosmicEcho - Synthrotek ECHO / PT2399-style delay for Rocksmith
 * Pedal_CosmicEcho.
 *
 * Local reference: pedals/cosmic echo.png. The schematic shows a PT2399 delay
 * with an op-amp CV delay control, feedback tone shaping, and op-amp input/
 * output mixers. Rocksmith exposes Time, Feedback, and Mix, so this fixes the
 * hidden tone/modulation character to a dark, spacey PT2399 echo.
 */
#include "DistrhoPlugin.hpp"
#include "CosmicEchoParams.h"
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

class CosmicEchoCore
{
    float sampleRate = 48000.0f;
    float time = kCosmicEchoDef[kTime];
    float feedback = kCosmicEchoDef[kFeedback];
    float mix = kCosmicEchoDef[kMix];

    DelayBuffer delayL;
    DelayBuffer delayR;
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

    float delaySmoothL = 420.0f;
    float delaySmoothR = 441.0f;
    float fbMemL = 0.0f;
    float fbMemR = 0.0f;
    float lpNoiseL = 0.0f;
    float lpNoiseR = 0.0f;
    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    uint32_t noiseState = 0x6d2b79f5u;

    float currentDelayMs() const
    {
        // Existing Rocksmith mapping stores Time as milliseconds / 2000.
        const float ms = time * 2000.0f;
        return std::fmax(55.0f, std::fmin(ms, 950.0f));
    }

    float noise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    }

    void updateFilters()
    {
        const float delayMs = currentDelayMs();
        const float t = clamp01(delayMs / 900.0f);
        const float fb = smoothstep(feedback);

        inHpL.setHighPass(sampleRate, 26.0f, 0.68f);
        inHpR.setHighPass(sampleRate, 26.0f, 0.68f);
        inLpL.setLowPass(sampleRate, 7200.0f - 850.0f * t, 0.67f);
        inLpR.setLowPass(sampleRate, 7050.0f - 820.0f * t, 0.67f);
        loopHpL.setHighPass(sampleRate, 82.0f + 58.0f * fb, 0.56f);
        loopHpR.setHighPass(sampleRate, 86.0f + 62.0f * fb, 0.56f);
        loopLp1L.setLowPass(sampleRate, 4550.0f - 1250.0f * t - 650.0f * fb, 0.55f);
        loopLp1R.setLowPass(sampleRate, 4350.0f - 1180.0f * t - 620.0f * fb, 0.55f);
        loopLp2L.setLowPass(sampleRate, 3900.0f - 900.0f * t - 520.0f * fb, 0.55f);
        loopLp2R.setLowPass(sampleRate, 3720.0f - 850.0f * t - 500.0f * fb, 0.55f);
        wetLpL.setLowPass(sampleRate, 5600.0f - 700.0f * t, 0.62f);
        wetLpR.setLowPass(sampleRate, 5400.0f - 700.0f * t, 0.62f);
    }

    float shapeRepeat(float x, float& n, float drive, float noiseAmount)
    {
        n += 0.013f * (noise() - n);
        const float noisy = x + n * noiseAmount;
        return std::tanh(noisy * drive) * (0.92f / drive);
    }

    float processOne(float input, DelayBuffer& delay, Biquad& inHp, Biquad& inLp,
                     Biquad& loopHp, Biquad& loopLp1, Biquad& loopLp2, Biquad& wetLp,
                     float& delaySmooth, float& fbMem, float& noiseMem,
                     float targetMs, float wobbleMs, float sideBias)
    {
        delaySmooth += 0.0010f * (targetMs - delaySmooth);
        const float readSamples = std::fmax(3.0f, (delaySmooth + wobbleMs) * 0.001f * sampleRate);
        float wet = delay.read(readSamples);
        wet = wetLp.process(wet);

        const float t = clamp01(delaySmooth / 900.0f);
        const float fb = 0.06f + feedback * 0.82f;
        const float inputTone = inLp.process(inHp.process(input));

        float loop = loopHp.process(wet + fbMem * 0.28f);
        loop = loopLp1.process(loop);
        loop = loopLp2.process(loop);
        loop = shapeRepeat(loop, noiseMem, 1.10f + 0.28f * feedback + 0.16f * t, 0.0016f + 0.0028f * t);
        fbMem = loop;

        const float write = inputTone + loop * fb;
        delay.write(std::tanh(write * (1.02f + 0.10f * sideBias)) * 0.96f);
        return wet;
    }

public:
    void reset()
    {
        delayL.reset();
        delayR.reset();
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
        delaySmoothR = currentDelayMs() * 1.035f;
        fbMemL = fbMemR = lpNoiseL = lpNoiseR = 0.0f;
        wowPhase = flutterPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delayL.resize((int)(sampleRate * 1.05f) + 64);
        delayR.resize((int)(sampleRate * 1.09f) + 64);
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
        const float t = clamp01(baseMs / 900.0f);

        wowPhase += (2.0f * kPi * (0.19f + 0.22f * t)) / sampleRate;
        flutterPhase += (2.0f * kPi * (3.8f + 2.2f * t)) / sampleRate;
        if (wowPhase >= 2.0f * kPi)
            wowPhase -= 2.0f * kPi;
        if (flutterPhase >= 2.0f * kPi)
            flutterPhase -= 2.0f * kPi;

        const float wobble = std::sin(wowPhase) * (0.9f + 3.0f * t)
                           + std::sin(flutterPhase) * (0.10f + 0.35f * t);

        const float monoIn = (inL + inR) * 0.5f;
        const float feedL = monoIn * 0.82f + inL * 0.18f;
        const float feedR = monoIn * 0.78f + inR * 0.22f;

        const float wetL = processOne(feedL, delayL, inHpL, inLpL, loopHpL, loopLp1L, loopLp2L, wetLpL,
                                      delaySmoothL, fbMemL, lpNoiseL, baseMs, wobble, -1.0f);
        const float wetR = processOne(feedR, delayR, inHpR, inLpR, loopHpR, loopLp1R, loopLp2R, wetLpR,
                                      delaySmoothR, fbMemR, lpNoiseR, baseMs * 1.035f, -wobble * 0.72f, 1.0f);

        const float wetGain = mix * (0.86f + 0.12f * feedback);
        const float dryGain = 1.0f - 0.32f * mix;
        outL = inL * dryGain + wetL * wetGain;
        outR = inR * dryGain + wetR * wetGain;
    }
};

class CosmicEchoPlugin : public Plugin
{
    CosmicEchoCore core;
    float params[kParamCount];

    void recalc()
    {
        core.setTime(params[kTime]);
        core.setFeedback(params[kFeedback]);
        core.setMix(params[kMix]);
    }

public:
    CosmicEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kCosmicEchoDef[i];
        core.setSampleRate((float)getSampleRate());
        recalc();
    }

protected:
    const char* getLabel() const override { return "CosmicEcho"; }
    const char* getDescription() const override { return "PT2399 cosmic echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 's', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kCosmicEchoNames[index];
        parameter.symbol = kCosmicEchoSymbols[index];
        parameter.ranges.min = kCosmicEchoMin[index];
        parameter.ranges.max = kCosmicEchoMax[index];
        parameter.ranges.def = kCosmicEchoDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CosmicEchoPlugin)
};

Plugin* createPlugin()
{
    return new CosmicEchoPlugin();
}

END_NAMESPACE_DISTRHO
