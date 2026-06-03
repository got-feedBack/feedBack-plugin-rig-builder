/*
 * Limiter - two-knob peak limiter for Rocksmith's Pedal_Limiter.
 *
 * Rocksmith exposes Limit and Rate only. Limit controls the amount/threshold;
 * Rate controls recovery speed. The detector is stereo-linked so limiting
 * does not pull the image left or right.
 */
#include "DistrhoPlugin.hpp"
#include "LimiterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

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
    return 20.0f * std::log10(amp + 1.0e-9f);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float coefMs(float ms, float sr)
{
    if (ms <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * sr));
}

static inline float softCeiling(float x, float ceiling)
{
    if (ceiling <= 0.0f)
        return 0.0f;
    return ceiling * std::tanh(x / ceiling);
}

} // namespace

class LimiterPlugin : public Plugin
{
    float params[kParamCount];
    float sampleRate = 48000.0f;

    float detector = 0.0f;
    float reductionDb = 0.0f;
    float toneL = 0.0f;
    float toneR = 0.0f;
    float hpXL = 0.0f;
    float hpYL = 0.0f;
    float hpXR = 0.0f;
    float hpYR = 0.0f;

    float thresholdDb = -18.0f;
    float ratio = 14.0f;
    float kneeDb = 3.5f;
    float makeupDb = 1.5f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
    float grAttackCoef = 0.0f;
    float grReleaseCoef = 0.0f;
    float toneCoef = 0.0f;
    float hpCoef = 0.0f;
    float ceiling = 0.92f;

    void recalc()
    {
        const float limit = clamp01(params[kLimit]);
        const float rate = clamp01(params[kRate]);
        const float amount = smoothstep(clamp01(0.18f + 1.45f * limit));

        thresholdDb = -5.5f - 31.0f * amount;
        ratio = 8.0f + 32.0f * amount;
        kneeDb = 5.5f - 2.6f * amount;
        makeupDb = std::fmin(5.8f, (-thresholdDb - 5.5f) * (0.14f + 0.15f * amount));

        const float attackMs = 0.22f + 1.20f * (1.0f - amount);
        const float releaseMs = 55.0f + 720.0f * std::pow(1.0f - rate, 1.55f);
        attackCoef = coefMs(attackMs, sampleRate);
        releaseCoef = coefMs(releaseMs, sampleRate);
        grAttackCoef = coefMs(0.60f + 1.8f * (1.0f - amount), sampleRate);
        grReleaseCoef = coefMs(releaseMs * 0.72f, sampleRate);
        toneCoef = 1.0f - std::exp(-2.0f * kPi * (13500.0f - 2600.0f * amount) / sampleRate);

        const float hpHz = 18.0f;
        const float dt = 1.0f / sampleRate;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpCoef = hpRc / (hpRc + dt);
        ceiling = dbToAmp(-0.45f - 0.65f * amount);
    }

    float highPass(float x, float& x1, float& y1)
    {
        const float y = hpCoef * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }

    float lowPass(float x, float& z)
    {
        z += toneCoef * (x - z);
        return z;
    }

    float targetReduction(float levelDb) const
    {
        const float over = levelDb - thresholdDb;
        if (over <= -0.5f * kneeDb)
            return 0.0f;

        const float slope = 1.0f - 1.0f / ratio;
        if (over >= 0.5f * kneeDb)
            return over * slope;

        const float x = over + 0.5f * kneeDb;
        return slope * (x * x) / (2.0f * kneeDb);
    }

public:
    LimiterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kLimiterDef[i];
        sampleRate = (float)getSampleRate();
        if (sampleRate <= 1000.0f)
            sampleRate = 48000.0f;
        recalc();
    }

protected:
    const char* getLabel() const override { return "Limiter"; }
    const char* getDescription() const override { return "two-knob stereo peak limiter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('L', 'i', 'm', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kLimiterNames[index];
        parameter.symbol = kLimiterSymbols[index];
        parameter.ranges.min = kLimiterMin[index];
        parameter.ranges.max = kLimiterMax[index];
        parameter.ranges.def = kLimiterDef[index];
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
        recalc();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        sampleRate = (float)newSampleRate;
        if (sampleRate <= 1000.0f)
            sampleRate = 48000.0f;
        detector = reductionDb = 0.0f;
        toneL = toneR = 0.0f;
        hpXL = hpYL = hpXR = hpYR = 0.0f;
        recalc();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float l = highPass(inL[i], hpXL, hpYL);
            const float r = highPass(inR[i], hpXR, hpYR);
            const float peak = std::fmax(std::fabs(l), std::fabs(r));
            const float envCoef = peak > detector ? attackCoef : releaseCoef;
            detector += envCoef * (peak - detector);

            const float levelDb = ampToDb(detector);
            const float targetDb = targetReduction(levelDb);
            const float grCoef = targetDb > reductionDb ? grAttackCoef : grReleaseCoef;
            reductionDb += grCoef * (targetDb - reductionDb);

            const float gain = dbToAmp(makeupDb - reductionDb);
            const float yL = lowPass(softCeiling(l * gain, ceiling), toneL);
            const float yR = lowPass(softCeiling(r * gain, ceiling), toneR);
            outL[i] = yL;
            outR[i] = yR;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterPlugin)
};

Plugin* createPlugin()
{
    return new LimiterPlugin();
}

END_NAMESPACE_DISTRHO
