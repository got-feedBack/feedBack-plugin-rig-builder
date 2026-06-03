/*
 * RingMod - Maestro/Oberheim RM-1A style ring modulator for Rocksmith's
 * Pedal_RingMod.
 *
 * The local schematic shows a preamp, oscillator, squelch/envelope section and
 * MC1495 balanced modulator with signal/carrier null trims. Rocksmith exposes
 * Depth, Waveform, Sensitivity and Attack, so the trim controls are fixed and
 * the carrier is driven by a dynamic oscillator.
 */
#include "DistrhoPlugin.hpp"
#include "RingModParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kTwoPi = 6.28318530718f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(16.0f, std::fmin(hz, nyquist));
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
    ms = std::fmax(0.05f, ms);
    return 1.0f - std::exp(-1.0f / (0.001f * ms * sr));
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = kTwoPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = kTwoPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = kTwoPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }
};

} // namespace

class RingModCore
{
    float sampleRate = 48000.0f;
    float depth = kRingModDef[kDepth];
    float waveform = kRingModDef[kWaveform];
    float sensitivity = kRingModDef[kSensitivity];
    float attack = kRingModDef[kAttack];

    Biquad inputHp;
    Biquad inputVoice;
    Biquad modLowPass;
    Biquad outputHp;
    Biquad outputLowPass;

    float phase = 0.0f;
    float env = 0.0f;
    float gate = 0.0f;
    float carrierHz = 90.0f;

    float envAttackA = 0.0f;
    float envReleaseA = 0.0f;
    float gateAttackA = 0.0f;
    float gateReleaseA = 0.0f;
    float freqSlewA = 0.0f;

    void updateFilters()
    {
        const float s = smoothstep(sensitivity);
        const float a = smoothstep(attack);
        const float w = smoothstep(waveform);

        inputHp.setHighPass(sampleRate, 24.0f + 48.0f * s, 0.70f);
        inputVoice.setPeaking(sampleRate, 850.0f + 540.0f * s, 0.78f,
                              1.0f + 3.0f * depth);
        modLowPass.setLowPass(sampleRate, 8200.0f - 2600.0f * w, 0.68f);
        outputHp.setHighPass(sampleRate, 30.0f + 30.0f * (1.0f - s), 0.68f);
        outputLowPass.setLowPass(sampleRate, 7200.0f + 3600.0f * (1.0f - w), 0.64f);

        // Higher Attack means a slower RM-1A-style squelch and carrier sweep.
        envAttackA = onePoleCoeffMs(1.4f + 180.0f * a, sampleRate);
        envReleaseA = onePoleCoeffMs(42.0f + 260.0f * (1.0f - s), sampleRate);
        gateAttackA = onePoleCoeffMs(2.0f + 130.0f * a, sampleRate);
        gateReleaseA = onePoleCoeffMs(58.0f + 120.0f * a, sampleRate);
        freqSlewA = onePoleCoeffMs(3.5f + 170.0f * a, sampleRate);
    }

    float carrierWave() const
    {
        const float sine = std::sin(kTwoPi * phase);
        const float tri = 1.0f - 4.0f * std::fabs(phase - 0.5f);
        const float square = std::tanh(sine * 8.5f);

        if (waveform <= 0.5f)
        {
            const float t = waveform * 2.0f;
            return sine * (1.0f - t) + tri * t;
        }

        const float t = (waveform - 0.5f) * 2.0f;
        return tri * (1.0f - t) + square * t;
    }

    void updateEnvelope(float x)
    {
        const float detectorGain = 1.2f + 8.5f * sensitivity;
        const float target = clamp01(std::fabs(x) * detectorGain);
        const float envA = target > env ? envAttackA : envReleaseA;
        env += envA * (target - env);

        const float threshold = 0.035f + 0.145f * (1.0f - sensitivity);
        const float gateTarget = smoothstep((env - threshold) / (0.055f + 0.25f * sensitivity));
        const float gateA = gateTarget > gate ? gateAttackA : gateReleaseA;
        gate += gateA * (gateTarget - gate);
    }

public:
    void reset()
    {
        inputHp.reset();
        inputVoice.reset();
        modLowPass.reset();
        outputHp.reset();
        outputLowPass.reset();
        phase = 0.0f;
        env = 0.0f;
        gate = 0.0f;
        carrierHz = 90.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    void setWaveform(float v)
    {
        waveform = clamp01(v);
        updateFilters();
    }

    void setSensitivity(float v)
    {
        sensitivity = clamp01(v);
        updateFilters();
    }

    void setAttack(float v)
    {
        attack = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        float x = inputHp.process(in);
        x = inputVoice.process(x);

        // Slight preamp color before the balanced modulator, matching the
        // RM-1A front-end without making Depth=0 louder than bypass.
        const float pre = softClip(x * (1.15f + 1.15f * depth)) * (0.92f + 0.12f * sensitivity);
        updateEnvelope(pre);

        const float s = smoothstep(sensitivity);
        const float baseHz = 23.0f + 140.0f * s + 520.0f * s * s;
        const float envBend = env * (70.0f + 760.0f * s) * (0.25f + 0.75f * depth);
        const float targetHz = std::fmin(1850.0f, baseHz + envBend);
        carrierHz += freqSlewA * (targetHz - carrierHz);

        phase += carrierHz / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float carrier = carrierWave();

        // MC1495-style balanced multiply with imperfect nulls. Squelch reduces
        // carrier artifacts between notes but never fully closes the effect.
        const float balanced = pre * carrier * (1.65f + 1.25f * depth);
        const float signalLeak = pre * (0.040f + 0.045f * (1.0f - sensitivity));
        const float carrierLeak = carrier * (0.008f + 0.018f * waveform) * (0.30f + depth) * gate;
        float wet = balanced + signalLeak + carrierLeak;
        wet = modLowPass.process(wet);
        wet = softClip(wet * (1.08f + 0.34f * depth));
        wet = outputHp.process(wet);
        wet = outputLowPass.process(wet);

        const float amount = std::pow(clamp01(depth), 0.55f) * (0.28f + 0.72f * (0.20f + 0.80f * gate));
        const float dryLevel = 1.0f - 0.66f * amount;
        const float wetLevel = 0.74f * amount;
        return softClip(in * dryLevel + wet * wetLevel) * 0.98f;
    }
};

class RingModPlugin : public Plugin
{
    RingModCore left;
    RingModCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setWaveform(params[kWaveform]);
        right.setWaveform(params[kWaveform]);
        left.setSensitivity(params[kSensitivity]);
        right.setSensitivity(params[kSensitivity]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
    }

public:
    RingModPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kRingModDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "RingMod"; }
    const char* getDescription() const override { return "Maestro RM-1A style ring modulator"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'g', 'M', 'd'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kRingModNames[index];
        parameter.symbol = kRingModSymbols[index];
        parameter.ranges.min = kRingModMin[index];
        parameter.ranges.max = kRingModMax[index];
        parameter.ranges.def = kRingModDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingModPlugin)
};

Plugin* createPlugin()
{
    return new RingModPlugin();
}

END_NAMESPACE_DISTRHO
