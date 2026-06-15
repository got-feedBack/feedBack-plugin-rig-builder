/*
 * AutoVibe - envelope-controlled vibrato/vibe for the game Pedal_AutoVibe.
 *
 * No exact schematic is available. The the game pedal has Sens, Attack,
 * Release, and Mix, so this models an auto-vibrato where the input envelope
 * brings in a BBD-style modulated delay. Attack and Release shape how the vibe
 * blooms after the note.
 */
#include "DistrhoPlugin.hpp"
#include "AutoVibeParams.h"
#include <cmath>
#include <vector>

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

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resize(int samples)
    {
        if (samples < 8)
            samples = 8;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(float delaySamples) const
    {
        const int size = (int)data.size();
        delaySamples = std::fmax(1.0f, std::fmin(delaySamples, (float)(size - 3)));
        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        const int i0 = (int)std::floor(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }

    void write(float x)
    {
        data[(size_t)writeIndex] = x;
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

} // namespace

class AutoVibeCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float sens = kAutoVibeDef[kSens];
    float attack = kAutoVibeDef[kAttack];
    float release = kAutoVibeDef[kRelease];
    float mix = kAutoVibeDef[kMix];

    DelayBuffer delay;
    float env = 0.0f;
    float lfoPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float dark = 0.0f;
    float hpA = 0.0f;
    float darkA = 0.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;

    void update()
    {
        const float attackMs = 3.0f + 360.0f * attack * attack;
        const float releaseMs = 18.0f + 920.0f * release * release;
        attackA = coeffMs(attackMs, sampleRate);
        releaseA = coeffMs(releaseMs, sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 28.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);
        darkA = 1.0f - std::exp(-2.0f * kPi * (3900.0f + 2800.0f * (1.0f - mix)) / sampleRate);
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
        delay.reset();
        env = 0.0f;
        lfoPhase = phaseOffset;
        hpX1 = hpY1 = dark = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.060f));
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
        const float target = clamp01(std::fabs(in) * (2.0f + 7.5f * smoothstep(sens)));
        const float coeff = target > env ? attackA : releaseA;
        env += coeff * (target - env);
        const float e = smoothstep(env);

        const float rateHz = 0.16f + 6.2f * e;
        lfoPhase += rateHz / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        float x = highPass(in);
        const float lfo = std::sin(kTwoPi * lfoPhase);
        const float depth = smoothstep(mix) * (0.18f + 0.82f * e);
        const float delayMs = 5.8f + lfo * (0.35f + 6.0f * depth);
        float wet = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(x);

        dark += darkA * (wet - dark);
        wet = dark + (wet - dark) * 0.20f;

        const float dryLevel = 1.0f - 0.82f * smoothstep(mix);
        const float wetLevel = 0.92f * smoothstep(mix);
        return (x * dryLevel + wet * wetLevel) * 0.98f;
    }
};

class AutoVibePlugin : public Plugin
{
    AutoVibeCore left;
    AutoVibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setParams(params[kSens], params[kAttack], params[kRelease], params[kMix]);
        right.setParams(params[kSens], params[kAttack], params[kRelease], params[kMix]);
    }

public:
    AutoVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAutoVibeDef[i];
        left.setPhaseOffset(0.0f);
        right.setPhaseOffset(0.25f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AutoVibe"; }
    const char* getDescription() const override { return "envelope controlled vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'u', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAutoVibeNames[index];
        parameter.symbol = kAutoVibeSymbols[index];
        parameter.ranges.min = kAutoVibeMin[index];
        parameter.ranges.max = kAutoVibeMax[index];
        parameter.ranges.def = kAutoVibeDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoVibePlugin)
};

Plugin* createPlugin()
{
    return new AutoVibePlugin();
}

END_NAMESPACE_DISTRHO
