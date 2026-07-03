/*
 * TremOle / Dyna-Trem - Keeley DynaTrem-inspired dynamic tremolo.
 *
 * No usable schematic is available in the local files: pedals/dyna-trem.pdf is
 * a 403 security-check page, not service notes. This model follows the known
 * product behaviour instead of pretending to be component-exact: dynamic rate,
 * dynamic depth, harmonic tremolo, Shape wave selection, and Level.
 *
 * the game's old Sens/Attack/Release/Mix surface is mapped to the real-style
 * controls in data/rs_knob_to_vst_param.json so existing songs still load.
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
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return 1.0f - std::exp(-1.0f / samples);
}

static inline float coeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float antiLog(float v)
{
    return std::pow(clamp01(v), 0.56f);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

} // namespace

class TremOleCore
{
    float sampleRate = 48000.0f;
    float stereoPhase = 0.0f;

    float rate = kTremOleDef[kRate];
    float depth = kTremOleDef[kDepth];
    float shape = kTremOleDef[kShape];
    float level = kTremOleDef[kLevel];
    float mode = kTremOleDef[kMode];

    float env = 0.0f;
    float phase = 0.0f;
    float gain = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float low = 0.0f;
    float highTone = 0.0f;
    float smear1 = 0.0f;
    float smear2 = 0.0f;

    float attackA = 0.0f;
    float releaseA = 0.0f;
    float hpA = 0.0f;
    float lowA = 0.0f;
    float toneA = 0.0f;
    float smearA = 0.0f;

    int modeIndex() const
    {
        if (mode < 0.333f)
            return 0; // Dynamic Rate
        if (mode < 0.666f)
            return 1; // Dynamic Depth
        return 2;     // Harmonic Tremolo
    }

    float baseRateHz() const
    {
        return 0.22f * std::pow(28.0f, rate);
    }

    void update()
    {
        attackA = coeffMs(4.0f + 24.0f * (1.0f - depth), sampleRate);
        releaseA = coeffMs(95.0f + 420.0f * (1.0f - rate), sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        lowA = coeffHz(760.0f + 520.0f * shape, sampleRate);
        toneA = coeffHz(7200.0f - 1500.0f * depth, sampleRate);
        smearA = coeffMs(24.0f + 90.0f * shape, sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float waveShape(float p) const
    {
        p -= std::floor(p);
        const float tri = 1.0f - std::fabs(2.0f * p - 1.0f);
        const float sine = 0.5f + 0.5f * std::sin(kTwoPi * (p - 0.25f));
        const float rampUp = p;
        const float rampDown = 1.0f - p;
        const float edge = 0.055f;
        const float square = clamp01(smoothstep(p / edge) * (1.0f - smoothstep((p - 0.50f) / edge)));

        if (shape < 0.02f)
            return tri; // hidden standard tremolo feel
        if (shape < 0.333f)
        {
            const float t = smoothstep(shape / 0.333f);
            return rampUp * (1.0f - t) + sine * t;
        }
        if (shape < 0.666f)
        {
            const float t = smoothstep((shape - 0.333f) / 0.333f);
            return sine * (1.0f - t) + rampDown * t;
        }
        const float t = smoothstep((shape - 0.666f) / 0.334f);
        return rampDown * (1.0f - t) + square * t;
    }

public:
    void setStereoPhase(float p)
    {
        stereoPhase = p;
    }

    void reset()
    {
        env = 0.0f;
        phase = stereoPhase;
        gain = 1.0f;
        hpX1 = hpY1 = low = highTone = smear1 = smear2 = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setParams(float ra, float de, float sh, float le, float mo)
    {
        rate = clamp01(ra);
        depth = clamp01(de);
        shape = clamp01(sh);
        level = clamp01(le);
        mode = clamp01(mo);
        update();
    }

    float process(float in)
    {
        const float detectorDrive = 2.2f + 7.0f * antiLog(depth);
        const float targetEnv = clamp01(std::fabs(in) * detectorDrive);
        const float envA = targetEnv > env ? attackA : releaseA;
        env += envA * (targetEnv - env);
        const float active = smoothstep(env);

        const int m = modeIndex();
        float rateHz = baseRateHz();
        float amount = antiLog(depth);
        if (m == 0)
            rateHz *= 0.12f + 2.75f * active;
        else if (m == 1)
            amount *= 0.10f + 0.92f * active;
        else
            amount *= 0.70f + 0.30f * active;

        phase += rateHz / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float lfo = waveShape(phase);
        const float invLfo = 1.0f - lfo;
        const float floor = 1.0f - 0.94f * amount;
        const float targetGain = floor + (1.0f - floor) * invLfo;
        gain += coeffMs(2.0f + 7.0f * (1.0f - shape), sampleRate) * (targetGain - gain);

        float x = highPass(in);
        highTone += toneA * (x - highTone);
        x = highTone;

        float y = x * gain;
        if (m == 2)
        {
            low += lowA * (x - low);
            const float high = x - low;
            const float highGain = floor + (1.0f - floor) * lfo;
            const float lowGain = floor + (1.0f - floor) * invLfo;
            smear1 += smearA * (low * lowGain - smear1);
            smear2 += smearA * (smear1 - smear2);
            const float harmonic = high * highGain + smear2 * (0.92f + 0.18f * shape);
            y = harmonic * (0.95f + 0.08f * amount);
        }

        const float outputLevel = 0.70f + 0.72f * level;
        return softClip(y * outputLevel) * 0.985f;
    }
};

class TremOlePlugin : public Plugin
{
    TremOleCore left;
    TremOleCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setParams(params[kRate], params[kDepth], params[kShape], params[kLevel], params[kMode]);
        right.setParams(params[kRate], params[kDepth], params[kShape], params[kLevel], params[kMode]);
    }

public:
    TremOlePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTremOleDef[i];
        left.setStereoPhase(0.0f);
        right.setStereoPhase(0.0f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TremOle"; }
    const char* getDescription() const override { return "dynamic harmonic tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
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
