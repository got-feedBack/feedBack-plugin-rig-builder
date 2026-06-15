/*
 * TremOle - envelope-aware stereo tremolo for the game Pedal_TremOle.
 *
 * No exact schematic is available. The pedal art exposes Sens, Attack,
 * Release, and Mix plus main/stereo outputs, so this model uses an envelope to
 * bring in a pulsing tremolo/panner. Mix is depth, Sens is detector drive, and
 * Attack/Release shape how quickly the tremolo grabs and lets go.
 */
#include "DistrhoPlugin.hpp"
#include "TremOleParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;

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

class TremOleCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float sens = kTremOleDef[kSens];
    float attack = kTremOleDef[kAttack];
    float release = kTremOleDef[kRelease];
    float mix = kTremOleDef[kMix];

    float env = 0.0f;
    float phase = 0.0f;
    float gain = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float tone = 0.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float hpA = 0.0f;
    float toneA = 0.0f;

    void update()
    {
        attackA = coeffMs(2.5f + 250.0f * attack * attack, sampleRate);
        releaseA = coeffMs(18.0f + 1150.0f * release * release, sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);
        toneA = 1.0f - std::exp(-2.0f * kPi * (7200.0f - 1900.0f * mix) / sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

public:
    void setPhaseOffset(float v)
    {
        phaseOffset = v;
    }

    void reset()
    {
        env = 0.0f;
        phase = phaseOffset;
        gain = 1.0f;
        hpX1 = hpY1 = tone = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setParams(float se, float at, float re, float mi)
    {
        sens = clamp01(se);
        attack = clamp01(at);
        release = clamp01(re);
        mix = clamp01(mi);
        update();
    }

    float process(float in)
    {
        const float target = clamp01(std::fabs(in) * (2.1f + 8.0f * smoothstep(sens)));
        const float coeff = target > env ? attackA : releaseA;
        env += coeff * (target - env);
        const float active = smoothstep(env);

        const float rateHz = 2.15f + 5.85f * active;
        phase += rateHz / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float s = std::sin(kTwoPi * phase);
        const float shaped = smoothstep(0.5f + 0.5f * s);
        const float depth = smoothstep(mix) * (0.12f + 0.88f * active);
        const float floor = 1.0f - 0.96f * depth;
        const float targetGain = floor + (1.0f - floor) * (1.0f - shaped);
        gain += coeffMs(2.0f + 8.0f * (1.0f - mix), sampleRate) * (targetGain - gain);

        float x = highPass(in);
        tone += toneA * (x - tone);
        return tone * gain * (0.985f + 0.025f * depth);
    }
};

class TremOlePlugin : public Plugin
{
    TremOleCore left;
    TremOleCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setParams(params[kSens], params[kAttack], params[kRelease], params[kMix]);
        right.setParams(params[kSens], params[kAttack], params[kRelease], params[kMix]);
    }

public:
    TremOlePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTremOleDef[i];
        left.setPhaseOffset(0.0f);
        right.setPhaseOffset(0.50f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TremOle"; }
    const char* getDescription() const override { return "envelope stereo tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'r', 'O', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTremOleNames[index];
        parameter.symbol = kTremOleSymbols[index];
        parameter.ranges.min = kTremOleMin[index];
        parameter.ranges.max = kTremOleMax[index];
        parameter.ranges.def = kTremOleDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TremOlePlugin)
};

Plugin* createPlugin()
{
    return new TremOlePlugin();
}

END_NAMESPACE_DISTRHO
