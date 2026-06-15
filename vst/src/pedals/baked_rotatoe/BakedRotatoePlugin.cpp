/*
 * BakedRotatoe - Leslie/RT-20 style rotary speaker for the game's
 * Pedal_BakedRotatoe.
 *
 * Local reference: pedals/baked rotatoe.pdf. The PDF is R.G. Keen's LERA,
 * which uses a 100uF capacitor and LED/LDR to make an effect speed control
 * accelerate and decelerate like a mechanical Leslie rotor. This DSP keeps
 * that ramping behavior and models the audible rotary cues with separated
 * drum/horn bands, doppler delay, tremolo and stereo phase motion.
 */
#include "DistrhoPlugin.hpp"
#include "BakedRotatoeParams.h"
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

static inline float onePoleCoeff(float hz, float sr)
{
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float slewCoeffMs(float ms, float sr)
{
    ms = std::fmax(1.0f, ms);
    return 1.0f - std::exp(-1.0f / (0.001f * ms * sr));
}

class DelayLine
{
    std::vector<float> data;
    size_t writePos = 0;

public:
    void resize(size_t size)
    {
        data.assign(size > 8 ? size : 8, 0.0f);
        writePos = 0;
    }

    void reset()
    {
        for (float& v : data)
            v = 0.0f;
        writePos = 0;
    }

    void write(float x)
    {
        if (data.empty())
            return;
        data[writePos] = x;
        writePos = (writePos + 1) % data.size();
    }

    float read(float delaySamples) const
    {
        if (data.empty())
            return 0.0f;

        const float size = (float)data.size();
        float pos = (float)writePos - delaySamples;
        while (pos < 0.0f)
            pos += size;
        while (pos >= size)
            pos -= size;

        const int i0 = (int)pos;
        const int i1 = (i0 + 1) % (int)data.size();
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }
};

class OnePole
{
    float a = 0.0f;
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    void setLowPass(float sr, float hz)
    {
        a = onePoleCoeff(hz, sr);
    }

    float lowPass(float x)
    {
        z += a * (x - z);
        return z;
    }
};

} // namespace

class BakedRotatoeCore
{
    float sampleRate = 48000.0f;
    float rate = kBakedRotatoeDef[kRate];
    float depth = kBakedRotatoeDef[kDepth];
    float mix = kBakedRotatoeDef[kMix];
    float balance = kBakedRotatoeDef[kBalance];

    DelayLine hornDelay;
    DelayLine drumDelay;
    OnePole crossover;
    OnePole hornToneL;
    OnePole hornToneR;
    OnePole drumToneL;
    OnePole drumToneR;

    float hornPhase = 0.0f;
    float drumPhase = 0.25f;
    float hornHz = 2.2f;
    float drumHz = 0.8f;
    float targetHornHz = 2.2f;
    float targetDrumHz = 0.8f;

    float dc = 0.0f;

    void updateTargets()
    {
        const float r = smoothstep(rate);
        // Boss-style continuous Rate, but with Leslie-ish slow/fast endpoints.
        targetHornHz = 0.42f + 6.40f * r;
        targetDrumHz = 0.16f + 2.05f * r;
    }

    void updateFilters()
    {
        crossover.setLowPass(sampleRate, 760.0f + 180.0f * balance);
        hornToneL.setLowPass(sampleRate, 5400.0f - 1150.0f * depth);
        hornToneR.setLowPass(sampleRate, 5400.0f - 1150.0f * depth);
        drumToneL.setLowPass(sampleRate, 1320.0f + 360.0f * balance);
        drumToneR.setLowPass(sampleRate, 1320.0f + 360.0f * balance);
        updateTargets();
    }

    void advanceRotors()
    {
        updateTargets();

        // LERA-style inertia: the virtual rotor speeds up faster than it slows
        // down, with both paths getting slightly snappier at high Rate.
        const float riseMs = 310.0f - 130.0f * rate;
        const float fallMs = 880.0f - 260.0f * rate;
        const float hornA = slewCoeffMs(targetHornHz > hornHz ? riseMs : fallMs, sampleRate);
        const float drumA = slewCoeffMs(targetDrumHz > drumHz ? riseMs * 1.65f : fallMs * 1.85f, sampleRate);
        hornHz += hornA * (targetHornHz - hornHz);
        drumHz += drumA * (targetDrumHz - drumHz);

        hornPhase += hornHz / sampleRate;
        drumPhase += drumHz / sampleRate;
        if (hornPhase >= 1.0f)
            hornPhase -= std::floor(hornPhase);
        if (drumPhase >= 1.0f)
            drumPhase -= std::floor(drumPhase);
    }

    float dopplerRead(const DelayLine& delay, float phase, float baseMs, float widthMs) const
    {
        const float pos = std::sin(kTwoPi * phase);
        const float delayMs = baseMs + widthMs * pos;
        return delay.read(delayMs * 0.001f * sampleRate);
    }

public:
    void reset()
    {
        hornDelay.reset();
        drumDelay.reset();
        crossover.reset();
        hornToneL.reset();
        hornToneR.reset();
        drumToneL.reset();
        drumToneR.reset();
        hornPhase = 0.0f;
        drumPhase = 0.25f;
        updateTargets();
        hornHz = targetHornHz;
        drumHz = targetDrumHz;
        dc = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        hornDelay.resize((size_t)(sampleRate * 0.060f));
        drumDelay.resize((size_t)(sampleRate * 0.080f));
        reset();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
        updateTargets();
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

    void setBalance(float v)
    {
        balance = clamp01(v);
        updateFilters();
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        advanceRotors();

        const float mono = 0.5f * (inL + inR);
        dc += 0.0008f * (mono - dc);
        const float x = mono - dc;

        const float low = crossover.lowPass(x);
        const float high = x - low;

        hornDelay.write(high);
        drumDelay.write(low);

        const float d = smoothstep(depth);
        const float b = clamp01(balance);
        const float hornMix = 0.28f + 0.92f * b;
        const float drumMix = 1.08f - 0.62f * b;

        const float hornWidthMs = (0.35f + 2.90f * d) * (0.88f + 0.32f * b);
        const float drumWidthMs = (0.16f + 1.15f * d) * (1.10f - 0.34f * b);

        float hornL = dopplerRead(hornDelay, hornPhase, 4.8f, hornWidthMs);
        float hornR = dopplerRead(hornDelay, hornPhase + 0.50f, 4.8f, hornWidthMs);
        float drumL = dopplerRead(drumDelay, drumPhase, 7.4f, drumWidthMs);
        float drumR = dopplerRead(drumDelay, drumPhase + 0.44f, 7.4f, drumWidthMs);

        hornL = hornToneL.lowPass(hornL);
        hornR = hornToneR.lowPass(hornR);
        drumL = drumToneL.lowPass(drumL);
        drumR = drumToneR.lowPass(drumR);

        const float hornAmpL = 0.76f + d * (0.32f + 0.24f * std::sin(kTwoPi * hornPhase));
        const float hornAmpR = 0.76f + d * (0.32f + 0.24f * std::sin(kTwoPi * (hornPhase + 0.50f)));
        const float drumAmpL = 0.88f + d * (0.13f + 0.12f * std::sin(kTwoPi * (drumPhase + 0.10f)));
        const float drumAmpR = 0.88f + d * (0.13f + 0.12f * std::sin(kTwoPi * (drumPhase + 0.54f)));

        float wetL = hornL * hornMix * hornAmpL + drumL * drumMix * drumAmpL;
        float wetR = hornR * hornMix * hornAmpR + drumR * drumMix * drumAmpR;

        // Cabinet/mic coloration and guard. Keep it colored, not boosted.
        wetL = softClip(wetL * (0.86f + 0.22f * d)) * 0.90f;
        wetR = softClip(wetR * (0.86f + 0.22f * d)) * 0.90f;

        const float m = clamp01(mix);
        const float dryLevel = 1.0f - 0.60f * m;
        const float wetLevel = m * (0.68f + 0.20f * d);
        outL = softClip(inL * dryLevel + wetL * wetLevel) * 0.98f;
        outR = softClip(inR * dryLevel + wetR * wetLevel) * 0.98f;
    }
};

class BakedRotatoePlugin : public Plugin
{
    BakedRotatoeCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setRate(params[kRate]);
        core.setDepth(params[kDepth]);
        core.setMix(params[kMix]);
        core.setBalance(params[kBalance]);
    }

public:
    BakedRotatoePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBakedRotatoeDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BakedRotatoe"; }
    const char* getDescription() const override { return "Leslie-style rotary speaker"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'k', 'R', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBakedRotatoeNames[index];
        parameter.symbol = kBakedRotatoeSymbols[index];
        parameter.ranges.min = kBakedRotatoeMin[index];
        parameter.ranges.max = kBakedRotatoeMax[index];
        parameter.ranges.def = kBakedRotatoeDef[index];
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
        core.setSampleRate((float)newSampleRate);
        applyAll();
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BakedRotatoePlugin)
};

Plugin* createPlugin()
{
    return new BakedRotatoePlugin();
}

END_NAMESPACE_DISTRHO
