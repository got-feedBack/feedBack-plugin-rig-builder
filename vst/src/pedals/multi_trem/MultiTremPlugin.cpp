/*
 * MultiTrem - Boss TR-2 style multi-wave tremolo for the game's
 * Pedal_MultiTrem. The local PDF is a TR-2 schematic: JFET input, LFO with a
 * Wave control, and a linear VCA. the game exposes Speed, Mix, and Waveform.
 */
#include "DistrhoPlugin.hpp"
#include "MultiTremParams.h"
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

static inline float antiLogPot(float v)
{
    return std::pow(clamp01(v), 0.70f);
}

} // namespace

class MultiTremCore
{
    float sampleRate = 48000.0f;
    float speed = kMultiTremDef[kSpeed];
    float mix = kMultiTremDef[kMix];
    float waveform = kMultiTremDef[kWaveform];

    float phase = 0.0f;
    float gainSmooth = 1.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float toneY = 0.0f;

    float gainA = 0.0f;
    float hpA = 0.0f;
    float toneA = 0.0f;

    float rateHz() const
    {
        // the game Multi Trem presets sit mostly around Speed 0.5-0.8. Make
        // 0.6 already feel medium-fast while keeping the top end usable.
        return 0.65f * std::pow(26.0f, speed);
    }

    void updateCoeffs()
    {
        gainA = onePoleCoeffMs(1.3f + 8.0f * (1.0f - waveform), sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 26.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        toneA = onePoleCoeffHz(7600.0f - 900.0f * antiLogPot(mix), sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float lowPass(float x)
    {
        toneY += toneA * (x - toneY);
        return toneY;
    }

    float lfoShape() const
    {
        const float p = phase - std::floor(phase);
        const float tri = 1.0f - std::fabs(2.0f * p - 1.0f);
        const float sine = 0.5f + 0.5f * std::sin((p - 0.25f) * 2.0f * kPi);

        const float edge = 0.030f + 0.090f * (1.0f - waveform);
        const float rise = smoothstep(p / edge);
        const float fall = 1.0f - smoothstep((p - 0.50f) / edge);
        const float square = clamp01(rise * fall);

        if (waveform < 0.5f)
        {
            const float t = smoothstep(waveform * 2.0f);
            return tri * (1.0f - t) + sine * t;
        }

        const float t = smoothstep((waveform - 0.5f) * 2.0f);
        return sine * (1.0f - t) + square * t;
    }

public:
    void reset()
    {
        phase = 0.0f;
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

    void setWaveform(float v)
    {
        waveform = clamp01(v);
        updateCoeffs();
    }

    float process(float in)
    {
        phase += rateHz() / sampleRate;
        if (phase >= 1.0f)
            phase -= 1.0f;

        const float depth = 0.05f + 0.95f * antiLogPot(mix);
        const float shape = lfoShape();
        const float maxCut = 0.90f + 0.08f * waveform;
        const float targetGain = 1.0f - maxCut * depth * shape;
        gainSmooth += gainA * (targetGain - gainSmooth);

        float x = highPass(in);
        x = lowPass(x);

        // TR-2 input/output utility stages are clean, but not perfectly ideal.
        x = 0.965f * x + 0.035f * softClip(x * (1.18f + 0.18f * depth));

        const float makeup = 1.0f + 0.06f * depth;
        return softClip(x * gainSmooth * makeup * 1.02f) * 0.985f;
    }
};

class MultiTremPlugin : public Plugin
{
    MultiTremCore left;
    MultiTremCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setWaveform(params[kWaveform]);
        right.setWaveform(params[kWaveform]);
    }

public:
    MultiTremPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMultiTremDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MultiTrem"; }
    const char* getDescription() const override { return "TR-2 style multi-wave tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('M', 'l', 'T', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMultiTremNames[index];
        parameter.symbol = kMultiTremSymbols[index];
        parameter.ranges.min = kMultiTremMin[index];
        parameter.ranges.max = kMultiTremMax[index];
        parameter.ranges.def = kMultiTremDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiTremPlugin)
};

Plugin* createPlugin()
{
    return new MultiTremPlugin();
}

END_NAMESPACE_DISTRHO
