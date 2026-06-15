/*
 * Phaser363 - MXR Phase 90 style one-knob phaser for the game's Pedal_Phaser.
 *
 * Local reference: pedals/phaser 363.png. The schematic is a four-stage JFET
 * all-pass phase shifter with one Rate control. the game exposes the same
 * single knob, so depth, feedback, and mix are fixed internally.
 */
#include "DistrhoPlugin.hpp"
#include "Phaser363Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kStageCount = 4;

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
    if (hz < 18.0f)
        return 18.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

class OnePoleFilter
{
    float lp = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float lpA = 0.0f;
    float hpA = 0.0f;

public:
    void reset()
    {
        lp = hpX1 = hpY1 = 0.0f;
    }

    void setLowPass(float sr, float hz)
    {
        lpA = onePoleCoeffHz(hz, sr);
    }

    void setHighPass(float sr, float hz)
    {
        const float dt = 1.0f / sr;
        const float rc = 1.0f / (2.0f * kPi * clampFreq(hz, sr));
        hpA = rc / (rc + dt);
    }

    float lowPass(float x)
    {
        lp += lpA * (x - lp);
        return lp;
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }
};

class FirstOrderAllpass
{
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    float process(float x, float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float t = std::tan(kPi * hz / sr);
        const float a = (1.0f - t) / (1.0f + t);
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class Phaser363Core
{
    float sampleRate = 48000.0f;
    float rate = kPhaser363Def[kRate];
    float phaseOffset = 0.0f;

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter toneLp;
    OnePoleFilter lfoLag;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    void updateFilters()
    {
        inputHp.setHighPass(sampleRate, 28.0f);
        toneLp.setLowPass(sampleRate, 6900.0f);
        lfoLag.setLowPass(sampleRate, 6.0f + 21.0f * rate);
    }

    float currentRateHz() const
    {
        return 0.055f + 5.75f * std::pow(clamp01(rate), 1.45f);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = 0.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        toneLp.reset();
        lfoLag.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(kTwoPi * phase);
        const float lfo = lfoLag.lowPass(std::pow(0.5f + 0.5f * sine, 1.22f));

        float x = inputHp.highPass(in);
        x = toneLp.lowPass(x);
        x = std::tanh(x * 1.04f) * 0.96f;

        static const float baseHz[kStageCount] = { 93.0f, 235.0f, 610.0f, 1540.0f };
        float shifted = x - feedback * 0.34f;
        for (int i = 0; i < kStageCount; ++i)
        {
            float stageLfo = lfo + 0.08f * (float)i;
            if (stageLfo > 1.0f)
                stageLfo -= 1.0f;
            const float sweep = 0.30f + 9.90f * smoothstep(stageLfo);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }

        feedback = std::tanh(shifted) * 0.32f;
        const float wet = std::tanh(shifted * 1.05f);
        // The dry−wet mix nearly cancels (allpass residual), leaving the output
        // ~20 dB below the rest of the pedals. Makeup brings it up to the house
        // ~unity level without changing the phaser's voicing. See AMP_LOUDNESS.md.
        const float y = (x * 0.70f - wet * 0.84f) * 8.5f;
        return std::tanh(y * 0.94f) * 0.98f;
    }
};

class Phaser363Plugin : public Plugin
{
    Phaser363Core left;
    Phaser363Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
    }

public:
    Phaser363Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPhaser363Def[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.015f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Phaser363"; }
    const char* getDescription() const override { return "MXR Phase 90 style one-knob phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', '3', '6', '3'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPhaser363Names[index];
        parameter.symbol = kPhaser363Symbols[index];
        parameter.ranges.min = kPhaser363Min[index];
        parameter.ranges.max = kPhaser363Max[index];
        parameter.ranges.def = kPhaser363Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Phaser363Plugin)
};

Plugin* createPlugin()
{
    return new Phaser363Plugin();
}

END_NAMESPACE_DISTRHO
