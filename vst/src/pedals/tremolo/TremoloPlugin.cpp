/*
 * Tremolo - Colorsound Tremola-style transistor tremolo for Rocksmith's
 * Pedal_Tremolo.
 *
 * Local reference: pedals/tremolo.jpg, a Colorsound Tremolo/Tremola version 1
 * schematic with a discrete transistor audio stage and a simple transistor LFO.
 * Rocksmith exposes Speed and Mix; Mix is the tremolo depth/intensity.
 */
#include "DistrhoPlugin.hpp"
#include "TremoloParams.h"
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
    return std::pow(clamp01(v), 0.62f);
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

class TremoloCore
{
    float sampleRate = 48000.0f;
    float speed = kTremoloDef[kSpeed];
    float mix = kTremoloDef[kMix];

    float phase = 0.0f;
    float lfoLag = 0.0f;
    float gainSmooth = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float toneY = 0.0f;

    float lfoRiseA = 0.0f;
    float lfoFallA = 0.0f;
    float gainA = 0.0f;
    float hpA = 0.0f;
    float toneA = 0.0f;

    float rateHz() const
    {
        // Common Rocksmith values are Speed 30-80. Put 56 around a clear
        // medium tremolo pulse and leave 80 fast without becoming chattery.
        return 0.62f * std::pow(30.0f, speed);
    }

    void updateCoeffs()
    {
        const float s = clamp01(speed);
        lfoRiseA = onePoleCoeffMs(6.0f + 10.0f * (1.0f - s), sampleRate);
        lfoFallA = onePoleCoeffMs(28.0f + 58.0f * (1.0f - s), sampleRate);
        gainA = onePoleCoeffMs(1.8f + 5.0f * (1.0f - s), sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 32.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        toneA = onePoleCoeffHz(8600.0f - 1450.0f * antiLogPot(mix), sampleRate);
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

    float lfoShape() const
    {
        const float p = phase - std::floor(phase);
        const float sine = 0.5f + 0.5f * std::sin((p - 0.25f) * 2.0f * kPi);
        const float tri = 1.0f - std::fabs(2.0f * p - 1.0f);

        // Transistor RC oscillator feel: faster attack, slower recovery, not a
        // hard square. This is the Colorsound character vs the smoother amp
        // tremolo and the sharper MultiTrem.
        const float rise = smoothstep(p / 0.24f);
        const float fall = 1.0f - smoothstep((p - 0.52f) / 0.48f);
        const float pulse = clamp01(rise * fall);

        return clamp01(0.42f * sine + 0.30f * tri + 0.28f * pulse);
    }

public:
    void reset()
    {
        phase = 0.0f;
        lfoLag = 0.0f;
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

    void setMix(float v)
    {
        mix = clamp01(v);
        updateCoeffs();
    }

    float process(float in)
    {
        phase += rateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= 1.0f;

        const float lfo = lfoShape();
        const float lfoA = lfo > lfoLag ? lfoRiseA : lfoFallA;
        lfoLag += lfoA * (lfo - lfoLag);

        const float depth = 0.03f + 0.97f * antiLogPot(mix);
        const float floor = 1.0f - 0.94f * depth;
        const float targetGain = floor + (1.0f - floor) * (1.0f - lfoLag);
        gainSmooth += gainA * (targetGain - gainSmooth);

        float x = highPass(in);
        x = toneLowPass(x);

        // Keep the transistor color restrained; the pedal should modulate
        // level, not add noticeable drive.
        const float makeup = 1.0f + 0.035f * depth;
        return x * gainSmooth * makeup * 0.99f;
    }
};

class TremoloPlugin : public Plugin
{
    TremoloCore left;
    TremoloCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    TremoloPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTremoloDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Tremolo"; }
    const char* getDescription() const override { return "Colorsound style transistor tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'r', 'M', 'o'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTremoloNames[index];
        parameter.symbol = kTremoloSymbols[index];
        parameter.ranges.min = kTremoloMin[index];
        parameter.ranges.max = kTremoloMax[index];
        parameter.ranges.def = kTremoloDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TremoloPlugin)
};

Plugin* createPlugin()
{
    return new TremoloPlugin();
}

END_NAMESPACE_DISTRHO
