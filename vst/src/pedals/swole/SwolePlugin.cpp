/*
 * Swole - heavy saturated compressor for the game Pedal_Swole.
 *
 * the game description: "Adds a very heavy saturated compression effect."
 * The local SG-1/Lazy Sprocket schematic is useful for the envelope/FET
 * control idea, but the game behavior is a smash compressor/saturator:
 * Smash controls ratio/saturation and Rate controls recovery speed.
 */
#include "DistrhoPlugin.hpp"
#include "SwoleParams.h"
#include <cmath>

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

static inline float coeffMs(float ms, float sr)
{
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * sr));
}

} // namespace

class SwoleCore
{
    float sampleRate = 48000.0f;
    float smash = kSwoleDef[kSmash];
    float rate = kSwoleDef[kRate];

    float env = 0.0f;
    float gain = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preTone = 0.0f;
    float postTone = 0.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float gainA = 0.0f;
    float hpA = 0.0f;
    float preToneA = 0.0f;
    float postToneA = 0.0f;

    void update()
    {
        const float s = smoothstep(smash);
        const float r = smoothstep(rate);
        attackA = coeffMs(1.0f + 12.0f * (1.0f - s), sampleRate);
        releaseA = coeffMs(26.0f + 620.0f * (1.0f - r), sampleRate);
        gainA = coeffMs(1.5f + 28.0f * (1.0f - r), sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        preToneA = 1.0f - std::exp(-2.0f * kPi * (5200.0f - 2100.0f * s) / sampleRate);
        postToneA = 1.0f - std::exp(-2.0f * kPi * (3600.0f + 1600.0f * (1.0f - s)) / sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

public:
    void reset()
    {
        env = 0.0f;
        gain = 1.0f;
        hpX1 = hpY1 = preTone = postTone = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setSmash(float v)
    {
        smash = clamp01(v);
        update();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
    }

    float process(float in)
    {
        const float s = smoothstep(smash);
        float x = highPass(in);
        preTone += preToneA * (x - preTone);
        x = preTone;

        const float detector = std::fabs(x) * (2.8f + 7.5f * s);
        const float coeff = detector > env ? attackA : releaseA;
        env += coeff * (detector - env);
        if (env < 0.0f)
            env = 0.0f;

        const float threshold = 0.090f - 0.068f * s;
        const float ratio = 2.4f + 17.5f * s;
        float targetGain = 1.0f;
        if (env > threshold)
        {
            const float over = env / threshold;
            const float compressed = std::pow(over, (1.0f / ratio) - 1.0f);
            targetGain = compressed;
        }
        gain += gainA * (targetGain - gain);

        const float makeup = 1.15f + 2.25f * s;
        float y = x * gain * makeup;
        const float satDrive = 1.1f + 4.9f * s;
        const float sym = std::tanh(y * satDrive);
        const float asym = std::tanh((y + 0.08f * s) * (satDrive * 1.18f)) - 0.04f * s;
        y = sym * (0.68f - 0.18f * s) + asym * (0.32f + 0.18f * s);
        y /= 1.0f + 0.38f * s;

        postTone += postToneA * (y - postTone);
        const float blend = 0.92f + 0.06f * s;
        return (postTone * blend + y * (1.0f - blend)) * (0.80f - 0.08f * s);
    }
};

class SwolePlugin : public Plugin
{
    SwoleCore left;
    SwoleCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSmash(params[kSmash]);
        right.setSmash(params[kSmash]);
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
    }

public:
    SwolePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSwoleDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Swole"; }
    const char* getDescription() const override { return "heavy saturated compressor"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'w', 'O', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSwoleNames[index];
        parameter.symbol = kSwoleSymbols[index];
        parameter.ranges.min = kSwoleMin[index];
        parameter.ranges.max = kSwoleMax[index];
        parameter.ranges.def = kSwoleDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwolePlugin)
};

Plugin* createPlugin()
{
    return new SwolePlugin();
}

END_NAMESPACE_DISTRHO
