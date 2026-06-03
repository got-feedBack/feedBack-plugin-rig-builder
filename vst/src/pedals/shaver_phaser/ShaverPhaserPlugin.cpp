/*
 * ShaverPhaser - ET-25B style phaser for Rocksmith's Pedal_ShaverPhaser.
 *
 * Local reference: pedals/shaver phaser.jpeg. The schematic uses Rate and
 * Depth around a compact JFET/OTA phase network. Rocksmith exposes only those
 * two controls, so mix and feedback are voiced internally.
 */
#include "DistrhoPlugin.hpp"
#include "ShaverPhaserParams.h"
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

class ShaverPhaserCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float rate = kShaverPhaserDef[kRate];
    float depth = kShaverPhaserDef[kDepth];

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter toneLp;
    OnePoleFilter lfoLag;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        inputHp.setHighPass(sampleRate, 30.0f);
        toneLp.setLowPass(sampleRate, 6500.0f - 1450.0f * d);
        lfoLag.setLowPass(sampleRate, 5.5f + 22.0f * rate);
    }

    float currentRateHz() const
    {
        return 0.075f + 6.40f * std::pow(clamp01(rate), 1.52f);
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

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.04f + 0.96f * smoothstep(depth);
        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(kTwoPi * phase);
        const float lfo = lfoLag.lowPass(0.5f + 0.5f * (0.74f * sine + 0.26f * std::sin(kTwoPi * (phase * 2.0f + 0.29f))));

        float x = inputHp.highPass(in);
        x = toneLp.lowPass(x);
        x = std::tanh(x * 1.04f) * 0.96f;

        static const float baseHz[kStageCount] = { 95.0f, 235.0f, 620.0f, 1500.0f };
        float shifted = x - feedback * (0.13f + 0.23f * d);
        for (int i = 0; i < kStageCount; ++i)
        {
            float stageLfo = lfo + 0.10f * (float)i;
            if (stageLfo > 1.0f)
                stageLfo -= 1.0f;
            const float sweep = 0.32f + (8.2f + 2.4f * d) * smoothstep(stageLfo);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }

        feedback = std::tanh(shifted) * (0.20f + 0.16f * d);
        const float wet = std::tanh(shifted * (1.02f + 0.10f * d));
        const float fixedMix = 0.50f + 0.32f * d;
        const float y = x * (0.86f - 0.20f * fixedMix) - wet * (0.56f + 0.34f * fixedMix);
        return std::tanh(y * 0.94f) * 0.98f;
    }
};

class ShaverPhaserPlugin : public Plugin
{
    ShaverPhaserCore left;
    ShaverPhaserCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    ShaverPhaserPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kShaverPhaserDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.02f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ShaverPhaser"; }
    const char* getDescription() const override { return "ET-25B style phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'h', 'P', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kShaverPhaserNames[index];
        parameter.symbol = kShaverPhaserSymbols[index];
        parameter.ranges.min = kShaverPhaserMin[index];
        parameter.ranges.max = kShaverPhaserMax[index];
        parameter.ranges.def = kShaverPhaserDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShaverPhaserPlugin)
};

Plugin* createPlugin()
{
    return new ShaverPhaserPlugin();
}

END_NAMESPACE_DISTRHO
