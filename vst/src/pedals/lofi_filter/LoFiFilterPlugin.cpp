/*
 * LoFiFilter - PedalPCB Lofinator-style OTA filter.
 * The local schematic has NJM4558 drive/recovery stages, 1N4148 clipping, two
 * NJM13600 OTA filter stages, a 2SC1815 support transistor and the real Drive,
 * Level, Lo and Hi controls. Rocksmith FilterType/Mix are translated in the
 * mapping table rather than exposed as fake plugin knobs.
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
    float drive = kLoFiFilterDef[kDrive];
    float level = kLoFiFilterDef[kLevel];
    float lo = kLoFiFilterDef[kLo];
    float hi = kLoFiFilterDef[kHi];

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
        const float d = std::sqrt(clamp01(drive));
        const float loPot = std::pow(clamp01(lo), 1.55f);  // A20K low-cut pot.
        const float hiPot = std::pow(clamp01(hi), 0.92f);  // B20K high-cut pot.

        float lowCut = 32.0f + 1450.0f * loPot;
        float highCut = 540.0f + 7600.0f * hiPot;
        if (highCut < lowCut * 2.15f)
            highCut = lowCut * 2.15f;

        const float q = 0.70f + 1.65f * d;
        inputHp.setHighPass(sampleRate, 36.0f, 0.70f);
        preLow.setLowPass(sampleRate, 9100.0f - 1900.0f * d, 0.72f);
        loStageA.setHighPass(sampleRate, lowCut, q);
        loStageB.setHighPass(sampleRate, lowCut * 0.82f + 42.0f, q * 0.86f);
        hiStageA.setLowPass(sampleRate, highCut, q * 0.92f);
        hiStageB.setLowPass(sampleRate, highCut * 0.76f + 350.0f, q * 0.82f);

        const float center = std::sqrt(lowCut * highCut);
        bandPeak.setBandPass(sampleRate, center, 1.05f + 2.80f * d);
        outputLow.setLowPass(sampleRate, 7600.0f - 2200.0f * d, 0.68f);

        const float hpHz = 24.0f;
        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * hpHz);
        dcA = rc / (rc + dt);

        holdPeriod = 1 + (int)std::floor(d * d * 6.0f + (1.0f - hiPot) * d * 3.0f);
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

    void setDrive(float v)
    {
        drive = clamp01(v);
        updateFilters();
    }

    void setLevel(float v)
    {
        level = clamp01(v);
    }

    void setLo(float v)
    {
        lo = clamp01(v);
        updateFilters();
    }

    void setHi(float v)
    {
        hi = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float d = std::sqrt(clamp01(drive));

        float x = inputHp.process(in);
        x = preLow.process(x);

        const float driveGain = 1.02f + 5.45f * d;
        float driven = x * driveGain;
        const float diode = std::tanh(driven * (0.82f + 0.35f * d));
        const float asym = std::tanh((driven + 0.10f * d) * (1.45f + 0.65f * d)) - 0.055f * d;
        driven = lerp(diode, asym, 0.34f + 0.38f * d);

        float filtered = loStageA.process(driven);
        filtered = loStageB.process(filtered);
        filtered = hiStageA.process(filtered);
        filtered = hiStageB.process(filtered);

        const float peak = bandPeak.process(driven) * (0.22f + 0.72f * d);
        float wet = filtered * (0.80f + 0.18f * d) + peak;

        const float fold = std::sin(wet * (1.2f + 1.55f * d)) * (0.04f + 0.12f * d);
        wet = wet + fold;

        if (holdPeriod > 1)
            wet = lerp(wet, sampleHold(wet), 0.20f + 0.34f * d);

        const float bits = 14.0f - 6.4f * d;
        const float levels = std::pow(2.0f, bits);
        wet = std::floor(wet * levels + (wet >= 0.0f ? 0.5f : -0.5f)) / levels;
        wet = outputLow.process(wet);

        // Lofinator is an inline filter, not a wet/dry blend. Keep a tiny
        // direct leakage to mimic op-amp feedthrough and preserve attack.
        float y = wet * (0.88f + 0.20f * d) + in * (0.045f + 0.025f * (1.0f - d));
        y = removeDc(y);
        const float outputTrim = (0.10f + 1.34f * clamp01(level)) / (1.0f + 0.20f * d);
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
        left.setDrive(params[kDrive]);
        right.setDrive(params[kDrive]);
        left.setLevel(params[kLevel]);
        right.setLevel(params[kLevel]);
        left.setLo(params[kLo]);
        right.setLo(params[kLo]);
        left.setHi(params[kHi]);
        right.setHi(params[kHi]);
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
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
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
