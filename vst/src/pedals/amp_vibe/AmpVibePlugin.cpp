/*
 * AmpVibe - Uni-Vibe style pedal for Rocksmith's Pedal_AmpVibe.
 * The local schematic shows four LDR-controlled phase stages driven by a
 * lamp oscillator, with chorus/vibrato output mixing. Rocksmith exposes only
 * Speed and Mix, so this models the chorus/vibe path: Speed drives the lamp
 * LFO, while Mix controls both phase blend and modulation depth.
 */
#include "DistrhoPlugin.hpp"
#include "AmpVibeParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr int kStageCount = 4;

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

static inline float onePoleCoeff(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
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
        lpA = onePoleCoeff(hz, sr);
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

class AmpVibeCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float speed = kAmpVibeDef[kSpeed];
    float mix = kAmpVibeDef[kMix];

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter toneLp;
    OnePoleFilter lampLag;
    OnePoleFilter outputHp;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    void updateFilters()
    {
        inputHp.setHighPass(sampleRate, 35.0f);
        toneLp.setLowPass(sampleRate, 6400.0f - 1800.0f * smoothstep(mix));
        lampLag.setLowPass(sampleRate, 12.0f + 24.0f * speed);
        outputHp.setHighPass(sampleRate, 24.0f);
    }

    float currentRateHz() const
    {
        return 0.22f * std::pow(24.0f, speed);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset;
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = 0.0f;
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

    void setSpeed(float v)
    {
        speed = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float rate = currentRateHz();
        lfoPhase += rate / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        const float sine = std::sin((lfoPhase + phaseOffset) * 2.0f * kPi);
        const float rawLfo = 0.5f + 0.5f * sine;
        const float lamp = lampLag.lowPass(std::pow(rawLfo, 1.38f));
        const float pulse = lamp * (1.12f + 0.18f * std::fabs(sine));
        const float intensity = 0.52f + 0.48f * smoothstep(mix);
        const float depth = 0.78f + 0.22f * intensity;

        float x = inputHp.highPass(in);
        x = toneLp.lowPass(x);
        x = std::tanh(x * 1.10f) * 0.96f;

        static const float baseHz[kStageCount] = { 62.0f, 155.0f, 420.0f, 1180.0f };
        float shifted = x + feedback * (0.30f + 0.34f * intensity);
        for (int i = 0; i < kStageCount; ++i)
        {
            const float stageOffset = 0.13f * (float)i;
            float ldr = pulse + stageOffset;
            if (ldr > 1.0f)
                ldr -= 1.0f;
            ldr = 0.03f + depth * smoothstep(ldr);
            const float sweep = 0.14f + 11.50f * ldr;
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }
        feedback = std::tanh(shifted) * (0.28f + 0.34f * intensity);

        const float throb = 1.0f - (0.18f + 0.28f * intensity) * (lamp * 2.0f - 1.0f);
        shifted *= throb;

        const float wet = shifted;
        const float dryLevel = 0.46f - 0.20f * intensity;
        const float wetLevel = 0.78f + 0.40f * intensity;
        float y = x * dryLevel - wet * wetLevel;
        y = outputHp.highPass(y);
        y = std::tanh(y * (0.96f + 0.10f * intensity)) * 0.88f;
        return y;
    }
};

class AmpVibePlugin : public Plugin
{
    AmpVibeCore left;
    AmpVibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    AmpVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAmpVibeDef[i];
        // The original Uni-Vibe is a mono phase/vibrato effect, not an
        // auto-pan. Keep both channels phase-linked so a centered guitar stays
        // centered while the phase network warbles inside the signal.
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AmpVibe"; }
    const char* getDescription() const override { return "Uni-Vibe style modulation"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 4); }
    int64_t getUniqueId() const override { return d_cconst('A', 'm', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAmpVibeNames[index];
        parameter.symbol = kAmpVibeSymbols[index];
        parameter.ranges.min = kAmpVibeMin[index];
        parameter.ranges.max = kAmpVibeMax[index];
        parameter.ranges.def = kAmpVibeDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpVibePlugin)
};

Plugin* createPlugin()
{
    return new AmpVibePlugin();
}

END_NAMESPACE_DISTRHO
