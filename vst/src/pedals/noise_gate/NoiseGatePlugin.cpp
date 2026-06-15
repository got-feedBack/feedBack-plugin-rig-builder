/*
 * NoiseGate - ET-45C/default-gate style expander for the game's
 * Pedal_NoiseGate. The reference schematic uses a detector/control path
 * around Sens and Decay, so this model uses a linked sidechain envelope and a
 * smoothed gain cell instead of hard muting the signal.
 */
#include "DistrhoPlugin.hpp"
#include "NoiseGateParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dbToAmp(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(amp + 1.0e-10f);
}

static inline float onePoleCoeff(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return 1.0f - std::exp(-1.0f / samples);
}

static inline float smoothstep(float lo, float hi, float x)
{
    if (hi <= lo)
        return x >= hi ? 1.0f : 0.0f;
    const float t = clamp01((x - lo) / (hi - lo));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

class NoiseGateCore
{
    float sampleRate = 48000.0f;
    float thresh = kNoiseGateDef[kThresh];
    float rate = kNoiseGateDef[kRate];

    float env = 0.0f;
    float gain = 1.0f;
    float hold = 0.0f;

    float thresholdDb = -60.0f;
    float kneeDb = 5.0f;
    float rangeDb = 42.0f;
    float holdSamples = 1200.0f;

    float envAttackA = 0.0f;
    float envReleaseA = 0.0f;
    float openA = 0.0f;
    float closeA = 0.0f;

    void updateCoeffs()
    {
        thresholdDb = -100.0f + 50.0f * thresh;

        // Higher Rate behaves like a tighter pedal gate: less knee, shorter
        // hold/decay, and a deeper floor. Low Rate stays deliberately gentle.
        kneeDb = 8.0f - 3.5f * rate;
        rangeDb = 14.0f + 46.0f * rate;
        holdSamples = sampleRate * (0.018f + 0.070f * (1.0f - rate));

        envAttackA = onePoleCoeff(0.45f, sampleRate);
        envReleaseA = onePoleCoeff(18.0f + 20.0f * (1.0f - rate), sampleRate);
        openA = onePoleCoeff(0.35f, sampleRate);
        closeA = onePoleCoeff(58.0f + 300.0f * (1.0f - rate), sampleRate);
    }

public:
    void reset()
    {
        env = 0.0f;
        gain = 1.0f;
        hold = 0.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setThresh(float v)
    {
        thresh = clamp01(v);
        updateCoeffs();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
        updateCoeffs();
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        const float detector = std::fmax(std::fabs(inL), std::fabs(inR));
        const float envA = detector > env ? envAttackA : envReleaseA;
        env += envA * (detector - env);

        const float envDb = ampToDb(env);
        const float openEdge = thresholdDb + 1.8f;
        const float closeEdge = thresholdDb - kneeDb;
        float open = smoothstep(closeEdge, openEdge, envDb);

        if (envDb > thresholdDb + 1.2f)
            hold = holdSamples;
        else if (hold > 0.0f)
        {
            hold -= 1.0f;
            open = std::fmax(open, 1.0f);
        }

        const float closed = 1.0f - open;
        const float shapedClosed = std::pow(closed, 1.15f + 0.45f * rate);
        const float targetGain = dbToAmp(-rangeDb * shapedClosed);
        const float gainA = targetGain > gain ? openA : closeA;
        gain += gainA * (targetGain - gain);

        outL = inL * gain;
        outR = inR * gain;
    }
};

class NoiseGatePlugin : public Plugin
{
    NoiseGateCore core;
    float params[kParamCount];

    void applyAll()
    {
        core.setThresh(params[kThresh]);
        core.setRate(params[kRate]);
    }

public:
    NoiseGatePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kNoiseGateDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NoiseGate"; }
    const char* getDescription() const override { return "ET-45C-style noise gate"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'g', 'a', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kNoiseGateNames[index];
        parameter.symbol = kNoiseGateSymbols[index];
        parameter.ranges.min = kNoiseGateMin[index];
        parameter.ranges.max = kNoiseGateMax[index];
        parameter.ranges.def = kNoiseGateDef[index];
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
        core.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
            core.process(inL[i], inR[i], outL[i], outR[i]);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGatePlugin)
};

Plugin* createPlugin()
{
    return new NoiseGatePlugin();
}

END_NAMESPACE_DISTRHO
