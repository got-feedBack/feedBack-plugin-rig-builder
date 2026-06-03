/*
 * OmniMod - Uni-Vibe/Shin-ei style photocell phase modulation for
 * Rocksmith's Pedal_OmniMod.
 *
 * Local references: pedals/omnimod_1.gif and pedals/omnimod_2.jpg. The
 * schematic family uses four phase stages driven by a lamp/LDR oscillator.
 * Rocksmith exposes Rate, Depth, and Mix.
 */
#include "DistrhoPlugin.hpp"
#include "OmniModParams.h"
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

class OmniModCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float rate = kOmniModDef[kRate];
    float depth = kOmniModDef[kDepth];
    float mix = kOmniModDef[kMix];

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter toneLp;
    OnePoleFilter lampLag;
    OnePoleFilter outputHp;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;
    float throbMemory = 0.0f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        inputHp.setHighPass(sampleRate, 34.0f);
        toneLp.setLowPass(sampleRate, 6600.0f - 1850.0f * d);
        lampLag.setLowPass(sampleRate, 4.5f + 19.0f * rate);
        outputHp.setHighPass(sampleRate, 24.0f);
    }

    float currentRateHz() const
    {
        return 0.075f + 6.85f * std::pow(clamp01(rate), 1.42f);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = throbMemory = 0.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        toneLp.reset();
        lampLag.reset();
        outputHp.reset();
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

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.04f + 0.96f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 1.04f * mix);
        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(kTwoPi * phase);
        const float lampRaw = 0.5f + 0.5f * (0.86f * sine + 0.14f * std::sin(kTwoPi * (phase * 2.0f + 0.18f)));
        const float lamp = lampLag.lowPass(std::pow(clamp01(lampRaw), 1.55f));

        float x = inputHp.highPass(in);
        x = toneLp.lowPass(x);
        x = std::tanh(x * 1.06f) * 0.95f;

        static const float baseHz[kStageCount] = { 58.0f, 145.0f, 420.0f, 1220.0f };
        float shifted = x + feedback * (0.22f + 0.28f * d);
        for (int i = 0; i < kStageCount; ++i)
        {
            float ldr = lamp + 0.14f * (float)i;
            if (ldr > 1.0f)
                ldr -= 1.0f;
            ldr = 0.025f + d * smoothstep(ldr);
            const float sweep = 0.16f + 12.8f * ldr;
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }

        feedback = std::tanh(shifted) * (0.20f + 0.28f * d + 0.08f * m);
        throbMemory += onePoleCoeffHz(9.0f, sampleRate) * ((lamp * 2.0f - 1.0f) - throbMemory);
        const float throb = 1.0f - (0.13f + 0.26f * d) * throbMemory;
        shifted *= throb;

        const float wet = shifted;
        const float dryLevel = 1.0f - 0.38f * m;
        const float wetLevel = (0.36f + 0.98f * m) * m;
        float y = x * dryLevel - wet * wetLevel;
        y = outputHp.highPass(y);
        return std::tanh(y * (0.94f + 0.10f * d)) * 0.96f;
    }
};

class OmniModPlugin : public Plugin
{
    OmniModCore left;
    OmniModCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    OmniModPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOmniModDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "OmniMod"; }
    const char* getDescription() const override { return "Uni-Vibe style photocell phase modulation"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'm', 'M', 'd'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOmniModNames[index];
        parameter.symbol = kOmniModSymbols[index];
        parameter.ranges.min = kOmniModMin[index];
        parameter.ranges.max = kOmniModMax[index];
        parameter.ranges.def = kOmniModDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OmniModPlugin)
};

Plugin* createPlugin()
{
    return new OmniModPlugin();
}

END_NAMESPACE_DISTRHO
