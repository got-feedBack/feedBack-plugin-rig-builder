/*
 * LoFiFilter - Lofinator-inspired filter for the game's Pedal_LoFiFilter.
 * The local PedalPCB schematic has an op-amp drive stage, clipping diodes,
 * two NJM13600 OTA filter stages, and Lo/Hi controls. the game exposes only
 * FilterType and Mix, so FilterType moves the Lo/Hi filter window while Mix
 * controls drive, resonance, texture, and wet intensity.
 */
#include "DistrhoPlugin.hpp"
#include "LoFiFilterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
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

} // namespace

class LoFiFilterCore
{
    float sampleRate = 48000.0f;
    float filterType = kLoFiFilterDef[kFilterType];
    float mix = kLoFiFilterDef[kMix];

    Biquad inputHp;
    Biquad preLow;
    Biquad loStageA;
    Biquad loStageB;
    Biquad hiStageA;
    Biquad hiStageB;
    Biquad bandPeak;
    Biquad outputLow;

    float dcX1 = 0.0f;
    float dcY1 = 0.0f;
    float dcA = 0.0f;
    float hold = 0.0f;
    int holdCounter = 0;
    int holdPeriod = 1;

    void updateFilters()
    {
        const float t = clamp01(filterType);
        const float amount = std::sqrt(clamp01(mix));

        const float lowNorm = std::pow(t, 1.35f);
        const float highNorm = std::pow(t, 0.78f);
        float lowCut = 52.0f + 1180.0f * lowNorm;
        float highCut = 620.0f + 7050.0f * highNorm;
        if (highCut < lowCut * 2.15f)
            highCut = lowCut * 2.15f;

        const float q = 0.70f + 1.85f * amount;
        inputHp.setHighPass(sampleRate, 36.0f, 0.70f);
        preLow.setLowPass(sampleRate, 8800.0f - 2200.0f * amount, 0.72f);
        loStageA.setHighPass(sampleRate, lowCut, q);
        loStageB.setHighPass(sampleRate, lowCut * 0.82f + 42.0f, q * 0.86f);
        hiStageA.setLowPass(sampleRate, highCut, q * 0.92f);
        hiStageB.setLowPass(sampleRate, highCut * 0.76f + 350.0f, q * 0.82f);

        const float center = std::sqrt(lowCut * highCut);
        bandPeak.setBandPass(sampleRate, center, 1.15f + 3.10f * amount);
        outputLow.setLowPass(sampleRate, 7600.0f - 2400.0f * amount, 0.68f);

        const float hpHz = 24.0f;
        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * hpHz);
        dcA = rc / (rc + dt);

        holdPeriod = 1 + (int)std::floor(amount * amount * 7.0f + (1.0f - t) * amount * 3.0f);
        if (holdPeriod < 1)
            holdPeriod = 1;
    }

    float removeDc(float x)
    {
        const float y = dcA * (dcY1 + x - dcX1);
        dcX1 = x;
        dcY1 = y;
        return y;
    }

    float sampleHold(float x)
    {
        if (holdCounter <= 0)
        {
            hold = x;
            holdCounter = holdPeriod;
        }
        --holdCounter;
        return hold;
    }

public:
    void reset()
    {
        dcX1 = dcY1 = hold = 0.0f;
        holdCounter = 0;
        inputHp.reset();
        preLow.reset();
        loStageA.reset();
        loStageB.reset();
        hiStageA.reset();
        hiStageB.reset();
        bandPeak.reset();
        outputLow.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setFilterType(float v)
    {
        filterType = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float amount = std::sqrt(clamp01(mix));
        const float t = clamp01(filterType);

        float x = inputHp.process(in);
        x = preLow.process(x);

        const float drive = 1.08f + 3.65f * amount;
        float driven = x * drive;
        const float diode = std::tanh(driven * (0.82f + 0.28f * amount));
        const float asym = std::tanh((driven + 0.10f * amount) * (1.45f + 0.55f * amount)) - 0.055f * amount;
        driven = lerp(diode, asym, 0.38f + 0.34f * amount);

        float filtered = loStageA.process(driven);
        filtered = loStageB.process(filtered);
        filtered = hiStageA.process(filtered);
        filtered = hiStageB.process(filtered);

        const float peak = bandPeak.process(driven) * (0.28f + 0.82f * amount);
        float wet = filtered * (0.76f + 0.22f * amount) + peak;

        const float fold = std::sin(wet * (1.2f + 1.7f * amount)) * (0.05f + 0.13f * amount);
        wet = wet + fold;

        if (holdPeriod > 1)
            wet = lerp(wet, sampleHold(wet), 0.26f + 0.38f * amount);

        const float bits = 14.0f - 7.0f * amount;
        const float levels = std::pow(2.0f, bits);
        wet = std::floor(wet * levels + (wet >= 0.0f ? 0.5f : -0.5f)) / levels;
        wet = outputLow.process(wet);

        // Low FilterType settings are intentionally darker and more choked;
        // high settings keep more dry edge for the Lofinator's bright band.
        // the game chains often place this pedal before clean amps, so the
        // filter must not work like an output boost when Mix is high.
        const float wetShare = 0.48f + 0.26f * amount;
        const float dryLevel = (1.0f - 0.42f * wetShare) * (0.94f + 0.06f * t);
        const float wetLevel = wetShare * (0.42f + 0.26f * amount);
        float y = in * dryLevel + wet * wetLevel;
        y = removeDc(y);
        const float outputTrim = 0.82f / (1.0f + 0.18f * amount);
        y = std::tanh(y * 0.92f) * outputTrim;
        return y;
    }
};

class LoFiFilterPlugin : public Plugin
{
    LoFiFilterCore left;
    LoFiFilterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setFilterType(params[kFilterType]);
        right.setFilterType(params[kFilterType]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    LoFiFilterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kLoFiFilterDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "LoFiFilter"; }
    const char* getDescription() const override { return "Lo-fi resonant filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('L', 'f', 'F', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kLoFiFilterNames[index];
        parameter.symbol = kLoFiFilterSymbols[index];
        parameter.ranges.min = kLoFiFilterMin[index];
        parameter.ranges.max = kLoFiFilterMax[index];
        parameter.ranges.def = kLoFiFilterDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoFiFilterPlugin)
};

Plugin* createPlugin()
{
    return new LoFiFilterPlugin();
}

END_NAMESPACE_DISTRHO
