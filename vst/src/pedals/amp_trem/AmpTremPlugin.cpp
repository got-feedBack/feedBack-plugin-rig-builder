/*
 * AmpTrem - Demeter Tremulator-style optical amp tremolo for the game's
 * Pedal_AmpTrem. The local schematic shows an op-amp LFO driving an LED/LDR
 * optocoupler into a TL061 audio stage. the game exposes Speed and Depth.
 */
#include "DistrhoPlugin.hpp"
#include "AmpTremParams.h"
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

static inline float antiLogPot(float v)
{
    v = clamp01(v);
    return std::pow(v, 0.46f);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return 1.0f - std::exp(-1.0f / samples);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

} // namespace

class AmpTremCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float speed = kAmpTremDef[kSpeed];
    float depth = kAmpTremDef[kDepth];

    float phase = 0.0f;
    float lamp = 0.0f;
    float gainSmooth = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float toneY = 0.0f;

    float lampRiseA = 0.0f;
    float lampFallA = 0.0f;
    float gainA = 0.0f;
    float hpA = 0.0f;
    float toneA = 0.0f;

    float rateHz() const
    {
        return 0.38f * std::pow(18.0f, speed);
    }

    void updateCoeffs()
    {
        // The LED responds fast; the LDR falls slower. Higher Speed tightens
        // the lag so fast the game settings do not smear into a flat level.
        lampRiseA = onePoleCoeffMs(5.0f + 7.0f * (1.0f - speed), sampleRate);
        lampFallA = onePoleCoeffMs(34.0f + 70.0f * (1.0f - speed), sampleRate);
        gainA = onePoleCoeffMs(3.0f, sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        toneA = onePoleCoeffHz(7200.0f - 1300.0f * antiLogPot(depth), sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float toneLowPass(float x)
    {
        toneY += toneA * (x - toneY);
        return toneY;
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset;
    }

    void reset()
    {
        phase = phaseOffset;
        lamp = 0.0f;
        gainSmooth = 1.0f;
        hpX1 = hpY1 = toneY = 0.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setSpeed(float v)
    {
        speed = clamp01(v);
        updateCoeffs();
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateCoeffs();
    }

    float process(float in)
    {
        phase += rateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= 1.0f;

        const float sine = std::sin((phase + phaseOffset) * 2.0f * kPi);
        const float tri = 1.0f - std::fabs(2.0f * (phase - std::floor(phase + 0.5f)));
        float led = 0.58f * (0.5f + 0.5f * sine) + 0.42f * tri;
        led = smoothstep(std::pow(clamp01(led), 1.05f));

        const float lampA = led > lamp ? lampRiseA : lampFallA;
        lamp += lampA * (led - lamp);

        // The schematic's Depth control sits after the opto stage. In
        // practice it behaves closer to an anti-log response than a linear
        // "percentage" knob: the middle of the travel should already pulse.
        const float d = 0.08f + 0.92f * antiLogPot(depth);
        const float optoCurve = std::pow(clamp01(lamp * 1.16f), 0.50f);
        const float floor = 0.94f - 0.91f * d;
        const float targetGain = floor + (1.0f - floor) * (1.0f - optoCurve);
        gainSmooth += gainA * (targetGain - gainSmooth);

        float x = highPass(in);
        x = toneLowPass(x);

        // TL061-ish utility stage: barely colored, but not mathematically dry.
        x = 0.94f * x + 0.06f * softClip(x * (1.20f + 0.35f * d));

        const float makeup = 1.0f + 0.08f * d;
        return softClip(x * gainSmooth * makeup * 1.03f) * 0.98f;
    }
};

class AmpTremPlugin : public Plugin
{
    AmpTremCore left;
    AmpTremCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    AmpTremPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAmpTremDef[i];
        left.setPhaseOffset(0.0f);
        right.setPhaseOffset(0.0f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AmpTrem"; }
    const char* getDescription() const override { return "Optical amp tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 2); }
    int64_t getUniqueId() const override { return d_cconst('A', 'm', 'T', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAmpTremNames[index];
        parameter.symbol = kAmpTremSymbols[index];
        parameter.ranges.min = kAmpTremMin[index];
        parameter.ranges.max = kAmpTremMax[index];
        parameter.ranges.def = kAmpTremDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpTremPlugin)
};

Plugin* createPlugin()
{
    return new AmpTremPlugin();
}

END_NAMESPACE_DISTRHO
