/*
 * BassPhase - Ibanez PH99 style opto phaser for Bass_Pedal_BassPhase.
 *
 * Local reference: pedals/ibanez_ph99.pdf. The schematic uses a NE571
 * compander around a six-stage 4558/C4570 all-pass network, TL022 LFO, two
 * P873-G35 optocouplers, and real Speed, Depth, Feedback, Level controls.
 */
#include "DistrhoPlugin.hpp"
#include "BassPhaseParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kStageCount = 6;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float reverseAudioTaper(float v)
{
    return std::pow(clamp01(v), 1.85f);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 16.0f)
        return 16.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

class OnePole
{
    float z = 0.0f;
    float a = 0.0f;

public:
    void reset() { z = 0.0f; }
    void setLowPass(float sr, float hz) { a = onePoleCoeffHz(hz, sr); }
    float process(float x)
    {
        z += a * (x - z);
        return z;
    }
};

class HighPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float a = 0.0f;

public:
    void reset() { x1 = y1 = 0.0f; }
    void set(float sr, float hz)
    {
        const float dt = 1.0f / sr;
        const float rc = 1.0f / (2.0f * kPi * clampFreq(hz, sr));
        a = rc / (rc + dt);
    }
    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class FirstOrderAllpass
{
    float z = 0.0f;

public:
    void reset() { z = 0.0f; }
    float process(float x, float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float t = std::tan(kPi * hz / sr);
        const float a = (t - 1.0f) / (t + 1.0f);   // FIX: break at fc (was near-Nyquist -> no audible phaser)
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class BassPhaseCore
{
    float sampleRate = 48000.0f;
    float speed = kBassPhaseDef[kSpeed];
    float depth = kBassPhaseDef[kDepth];
    float feedbackKnob = kBassPhaseDef[kFeedback];
    float level = kBassPhaseDef[kLevel];
    float phaseOffset = 0.0f;

    FirstOrderAllpass stages[kStageCount];
    HighPass inputHp;
    HighPass wetLowGuard;
    OnePole inputTone;
    OnePole outputTone;
    OnePole lfoLagA;
    OnePole lfoLagB;
    OnePole compEnv;

    float lfoPhase = 0.0f;
    float feedbackState = 0.0f;
    float companderGain = 1.0f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        const float fb = smoothstep(feedbackKnob);
        inputHp.set(sampleRate, 24.0f);
        wetLowGuard.set(sampleRate, 74.0f);
        inputTone.setLowPass(sampleRate, 8500.0f - 900.0f * fb);
        outputTone.setLowPass(sampleRate, 7800.0f - 1100.0f * d - 700.0f * fb);
        lfoLagA.setLowPass(sampleRate, 8.0f);
        lfoLagB.setLowPass(sampleRate, 3.4f);
        compEnv.setLowPass(sampleRate, 34.0f);
    }

    float currentRateHz() const
    {
        return 0.045f + 6.25f * reverseAudioTaper(speed);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedbackState = 0.0f;
        companderGain = 1.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        wetLowGuard.reset();
        inputTone.reset();
        outputTone.reset();
        lfoLagA.reset();
        lfoLagB.reset();
        compEnv.reset();
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

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    void setFeedback(float v)
    {
        feedbackKnob = clamp01(v);
        updateFilters();
    }

    void setLevel(float v)
    {
        level = clamp01(v);
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.08f + 0.92f * smoothstep(depth);
        const float fb = smoothstep(feedbackKnob);
        const float p = lfoPhase + phaseOffset;

        // TL022 oscillator in the PH99 is closer to rounded triangle than a
        // pure sine. The two P873-G35 optos do not track identically, so keep
        // two lagged control voltages with slight offset.
        const float wrapped = p - std::floor(p);
        const float tri = 1.0f - 4.0f * std::fabs(wrapped - 0.5f);
        const float sine = std::sin(kTwoPi * wrapped);
        const float osc = clamp01(0.5f + 0.5f * (0.58f * tri + 0.42f * sine));
        const float optoA = lfoLagA.process(clamp01(0.50f + (osc - 0.5f) * d));
        const float optoB = lfoLagB.process(clamp01(0.50f + ((1.0f - osc) - 0.5f) * (0.72f * d)));

        float x = inputHp.process(in);
        x = inputTone.process(x);

        // NE571-like level control: fast enough to keep the phase path even,
        // slow enough to avoid audible pumping. This is not a hard limiter.
        const float env = compEnv.process(std::fabs(x));
        const float targetGain = 1.0f / std::sqrt(0.18f + 5.5f * env);
        companderGain += 0.0065f * (targetGain - companderGain);
        float driven = std::tanh(x * (1.08f + 0.28f * companderGain)) * 0.93f;

        static const float baseHz[kStageCount] = { 118.0f, 190.0f, 315.0f, 525.0f, 875.0f, 1450.0f };
        const float regen = 0.10f + 0.62f * fb;
        float shifted = driven - feedbackState * regen;

        for (int i = 0; i < kStageCount; ++i)
        {
            const float cv = (i & 1) ? optoB : optoA;
            const float stageSkew = 0.90f + 0.075f * (float)i;
            const float sweep = 0.42f + (8.8f + 5.4f * d) * smoothstep(cv);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep * stageSkew);
        }

        feedbackState = std::tanh(shifted * (0.95f + 0.20f * fb));

        const float wetFull = outputTone.process(std::tanh(shifted * (1.04f + 0.18f * fb)));
        const float wetBassSafe = wetFull - wetLowGuard.process(wetFull);
        const float wet = wetBassSafe * 0.74f + wetFull * 0.26f;

        const float effectAmount = 0.58f + 0.30f * d + 0.16f * fb;
        const float outLevel = 0.38f + 1.05f * level;
        const float y = driven * 0.78f - wet * effectAmount;
        return std::tanh(y * 2.55f) * 0.60f * outLevel;
    }
};

class BassPhasePlugin : public Plugin
{
    BassPhaseCore left;
    BassPhaseCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setFeedback(params[kFeedback]);
        right.setFeedback(params[kFeedback]);
        left.setLevel(params[kLevel]);
        right.setLevel(params[kLevel]);
    }

public:
    BassPhasePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassPhaseDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.017f);
        const float sr = (float)getSampleRate();
        left.setSampleRate(sr);
        right.setSampleRate(sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassPhase"; }
    const char* getDescription() const override { return "Ibanez PH99 style opto phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'P', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassPhaseNames[index];
        parameter.symbol = kBassPhaseSymbols[index];
        parameter.ranges.min = kBassPhaseMin[index];
        parameter.ranges.max = kBassPhaseMax[index];
        parameter.ranges.def = kBassPhaseDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassPhasePlugin)
};

Plugin* createPlugin()
{
    return new BassPhasePlugin();
}

END_NAMESPACE_DISTRHO
