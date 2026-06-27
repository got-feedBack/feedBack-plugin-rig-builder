/*
 * RangeBooster - Rangemaster-style treble booster for the game's
 * Pedal_RangeBooster. Reference: local Rangemaster schematic with C1=5nF,
 * R1=470k, R2=68k, OC44 PNP germanium transistor, R3=3.9k bypassed by C3=47uF,
 * VR1=A10k collector-load Boost, and C4=10nF output coupling. The project has a
 * 2N1305 germanium datasheet on hand rather than an OC44 sheet, so its hFE,
 * leakage, and capacitance ranges are used only as a conservative Ge reference.
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

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.75f);
}

static inline float hpCoeff(float hz, float sr)
{
    const float pi = 3.14159265359f;
    const float dt = 1.0f / sr;
    const float rc = 1.0f / (2.0f * pi * hz);
    return rc / (rc + dt);
}

static inline float lpCoeff(float hz, float sr)
{
    const float pi = 3.14159265359f;
    return 1.0f - std::exp(-2.0f * pi * hz / sr);
}

static inline float geCommonEmitterCurve(float x, float posHeadroom, float negHeadroom)
{
    return x >= 0.0f ? posHeadroom * softClip(x / posHeadroom)
                     : negHeadroom * softClip(x / negHeadroom);
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
    float millerY = 0.0f;
    float collectorY = 0.0f;
    float biasMemory = 0.0f;
    float rectifiedInput = 0.0f;

    float inputHpA = 0.0f;
    float outputHpA = 0.0f;
    float millerA = 0.0f;
    float collectorA = 0.0f;
    float rectifierA = 0.0f;
    float biasA = 0.0f;

    float collectorNorm = 0.0f;
    float stageDrive = 1.0f;
    float posHeadroom = 0.42f;
    float negHeadroom = 0.70f;
    float dcBias = -0.04f;
    float outputLevel = 1.0f;

    void updateFilters()
    {
        const float pi = 3.14159265359f;
        const float c1 = 5.0e-9f;
        const float r1 = 470.0e3f;
        const float r2 = 68.0e3f;
        const float c4 = 10.0e-9f;
        const float nextAmpInput = 470.0e3f;

        const float pot = audioTaper(boost);
        collectorNorm = 0.08f + 0.92f * pot;

        // C1 feeding the R1/R2 bias ladder is the main Rangemaster treble shelf.
        // The transistor base loads that network a little harder as collector
        // load/gain rises, so the corner moves only slightly with Boost.
        const float rBias = 1.0f / (1.0f / r1 + 1.0f / r2);
        const float inputLoad = rBias * (0.96f - 0.16f * collectorNorm);
        const float inputHpHz = 1.0f / (2.0f * pi * c1 * inputLoad);
        inputHpA = hpCoeff(inputHpHz, sampleRate);

        // C4 is a coupling cap into the following amp/input, not the tone shaper.
        const float outputHpHz = 1.0f / (2.0f * pi * c4 * nextAmpInput);
        outputHpA = hpCoeff(outputHpHz, sampleRate);

        // Approximate OC44/2N1305 germanium capacitances. Cob is multiplied by
        // common-emitter gain (Miller), so max Boost naturally dulls the fizz.
        const float smallSignalGain = 8.0f + 22.0f * collectorNorm;
        const float millerCap = 9.0e-12f + 20.0e-12f * (1.0f + smallSignalGain);
        float millerHz = 1.0f / (2.0f * pi * 22.0e3f * millerCap);
        if (millerHz < 7800.0f)
            millerHz = 7800.0f;
        if (millerHz > 21000.0f)
            millerHz = 21000.0f;
        millerA = lpCoeff(millerHz, sampleRate);

        // A small collector/cable pole keeps the one-transistor boost bright but
        // not metallic when it hits clean amp inputs directly.
        collectorA = lpCoeff(15000.0f - 5200.0f * collectorNorm, sampleRate);

        rectifierA = lpCoeff(42.0f, sampleRate);
        biasA = lpCoeff(7.0f, sampleRate);

        // VR1 is the A10k collector-load control. More resistance gives more gain
        // and less clean headroom, just like turning the real Boost up.
        stageDrive = 1.65f + 10.8f * collectorNorm;
        posHeadroom = 0.44f - 0.11f * collectorNorm;
        negHeadroom = 0.78f - 0.10f * collectorNorm;
        dcBias = -0.030f - 0.044f * collectorNorm;
        outputLevel = 0.42f + 0.82f * collectorNorm;
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
        millerY = collectorY = biasMemory = rectifiedInput = 0.0f;
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

        // Germanium leakage and bias drift are low-frequency effects. Keep them
        // small and slow so the pedal compresses like Ge without pumping volume.
        rectifiedInput += rectifierA * (std::fabs(x) - rectifiedInput);
        const float targetBiasShift = -0.020f * collectorNorm * rectifiedInput;
        biasMemory += biasA * (targetBiasShift - biasMemory);

        const float bias = dcBias + biasMemory;
        const float driven = x * stageDrive + bias;
        const float idle = geCommonEmitterCurve(bias, posHeadroom, negHeadroom);
        float y = geCommonEmitterCurve(driven, posHeadroom, negHeadroom) - idle;

        // Common-emitter phase inversion is musically irrelevant here, but the
        // inverted sign keeps this stage consistent with the physical collector.
        y = -y;

        y = lowPass(y, millerY, millerA);
        y = lowPass(y, collectorY, collectorA);
        y = outputHighPass(y);
        return y * outputLevel;
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
