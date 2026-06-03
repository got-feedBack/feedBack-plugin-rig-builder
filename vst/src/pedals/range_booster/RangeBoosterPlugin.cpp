/*
 * RangeBooster - Rangemaster-style treble booster for Rocksmith's
 * Pedal_RangeBooster. Reference: single OC44 germanium transistor, tiny input
 * coupling cap, fixed bias, and one Boost pot. Rocksmith exposes only Boost,
 * so the DSP uses that one control for range emphasis and transistor color.
 */
#include "DistrhoPlugin.hpp"
#include "RangeBoosterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float asymClip(float x, float posDrive, float negDrive)
{
    return x >= 0.0f ? softClip(x * posDrive) : softClip(x * negDrive);
}

} // namespace

class RangeBoosterCore
{
    float sampleRate = 48000.0f;
    float boost = kRangeBoosterDef[kBoost];

    float inputHpX1 = 0.0f;
    float inputHpY1 = 0.0f;
    float outputHpX1 = 0.0f;
    float outputHpY1 = 0.0f;
    float brightY = 0.0f;
    float topY = 0.0f;

    float inputHpA = 0.0f;
    float outputHpA = 0.0f;
    float brightA = 0.0f;
    float topA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;

        // 5nF input cap into the Rangemaster bias network: intentionally
        // high-passed, but not so thin that low guitar notes disappear.
        const float inputHpHz = 180.0f + 610.0f * boost;
        const float inputHpRc = 1.0f / (2.0f * 3.14159265359f * inputHpHz);
        inputHpA = inputHpRc / (inputHpRc + dt);

        const float outputHpHz = 58.0f + 32.0f * boost;
        const float outputHpRc = 1.0f / (2.0f * 3.14159265359f * outputHpHz);
        outputHpA = outputHpRc / (outputHpRc + dt);

        const float brightHz = 760.0f + 1320.0f * boost;
        brightA = 1.0f - std::exp(-2.0f * 3.14159265359f * brightHz / sampleRate);

        const float topHz = 6800.0f + 5200.0f * (1.0f - boost);
        topA = 1.0f - std::exp(-2.0f * 3.14159265359f * topHz / sampleRate);
    }

    float inputHighPass(float x)
    {
        const float y = inputHpA * (inputHpY1 + x - inputHpX1);
        inputHpX1 = x;
        inputHpY1 = y;
        return y;
    }

    float outputHighPass(float x)
    {
        const float y = outputHpA * (outputHpY1 + x - outputHpX1);
        outputHpX1 = x;
        outputHpY1 = y;
        return y;
    }

    float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

public:
    void reset()
    {
        inputHpX1 = inputHpY1 = outputHpX1 = outputHpY1 = 0.0f;
        brightY = topY = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setBoost(float v)
    {
        boost = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        float x = inputHighPass(in);

        // Treble emphasis around the transistor input. Low Boost keeps body;
        // high Boost increasingly shifts the pedal toward focused upper mids.
        const float low = lowPass(x, brightY, brightA);
        const float high = x - (0.58f + 0.18f * boost) * low;
        x = low * (0.42f - 0.20f * boost) + high * (1.10f + 1.65f * boost);

        // One germanium transistor stage. Rocksmith presets often put this in
        // front of clean amps, so the transistor adds bite without acting like
        // a hidden output knob.
        const float boost2 = boost * boost;
        const float stageGain = 1.05f + 2.05f * boost + 0.85f * boost2;
        const float bias = -0.024f - 0.024f * boost;
        float y = x * stageGain + bias;
        y = asymClip(y, 0.78f + 0.55f * boost, 0.62f + 0.42f * boost);

        // Output cap and mild top-end smoothing.
        y = outputHighPass(y);
        y = lowPass(y, topY, topA);

        // Rocksmith does not expose output level here. Keep the pedal close to
        // unity and let Boost mostly change the emphasized frequency range.
        const float level = 0.70f + 0.40f * boost;
        return y * level;
    }
};

class RangeBoosterPlugin : public Plugin
{
    RangeBoosterCore left;
    RangeBoosterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setBoost(params[kBoost]);
        right.setBoost(params[kBoost]);
    }

public:
    RangeBoosterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kRangeBoosterDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "RangeBooster"; }
    const char* getDescription() const override { return "Rangemaster-style treble booster"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'b', 's', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kRangeBoosterNames[index];
        parameter.symbol = kRangeBoosterSymbols[index];
        parameter.ranges.min = kRangeBoosterMin[index];
        parameter.ranges.max = kRangeBoosterMax[index];
        parameter.ranges.def = kRangeBoosterDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RangeBoosterPlugin)
};

Plugin* createPlugin()
{
    return new RangeBoosterPlugin();
}

END_NAMESPACE_DISTRHO
