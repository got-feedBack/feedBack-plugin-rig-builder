/*
 * MultiPitch - MF-102 style ring mod / pitch sideband pedal for the game's
 * Pedal_MultiPitch.
 *
 * Local reference: pedals/multipitch.pdf. The schematic shows an MF-102 style
 * preamp, carrier oscillator, LFO and LM13600 balanced modulator. the game
 * exposes Pitch1, Tone and Mix; Pitch1 drives the carrier pitch while Tone
 * voices carrier shape and LFO amount.
 */
#include "DistrhoPlugin.hpp"
#include "MultiPitchParams.h"
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

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(8.0f, std::fmin(hz, nyquist));
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
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

class MultiPitchCore
{
    float sampleRate = 48000.0f;
    float pitch1 = kMultiPitchDef[kPitch1];
    float tone = kMultiPitchDef[kTone];
    float mix = kMultiPitchDef[kMix];

    Biquad inputHp;
    Biquad inputVoice;
    Biquad modLowPass;
    Biquad outputHp;
    Biquad outputLowPass;

    float carrierPhase = 0.0f;
    float lfoPhase = 0.0f;
    float carrierHz = 55.0f;
    float carrierSlewA = 0.0f;
    float ampEnv = 0.0f;

    void updateFilters()
    {
        const float t = smoothstep(tone);
        inputHp.setHighPass(sampleRate, 24.0f + 28.0f * (1.0f - t), 0.70f);
        inputVoice.setPeaking(sampleRate, 760.0f + 780.0f * t, 0.78f, 1.2f + 2.8f * t);
        modLowPass.setLowPass(sampleRate, 4200.0f + 6200.0f * t, 0.66f);
        outputHp.setHighPass(sampleRate, 28.0f, 0.68f);
        outputLowPass.setLowPass(sampleRate, 5200.0f + 5200.0f * t, 0.66f);
        carrierSlewA = onePoleCoeffHz(14.0f + 32.0f * t, sampleRate);
    }

    float pitchCarrierHz() const
    {
        // Mapping stores the game semitone Pitch1 as normalized:
        // -24 -> 0.0, 0 -> 0.5, +24 -> 1.0. Use A2 as the center carrier.
        const float semis = (pitch1 - 0.5f) * 48.0f;
        return std::fmax(12.0f, std::fmin(3600.0f, 110.0f * std::pow(2.0f, semis / 12.0f)));
    }

    float carrierWave(float phase) const
    {
        phase -= std::floor(phase);
        const float sine = std::sin(kTwoPi * phase);
        const float tri = 1.0f - 4.0f * std::fabs(phase - 0.5f);
        const float square = std::tanh(sine * 7.0f);
        const float t = smoothstep(tone);
        if (t < 0.55f)
        {
            const float a = t / 0.55f;
            return sine * (1.0f - a) + tri * a;
        }
        const float a = (t - 0.55f) / 0.45f;
        return tri * (1.0f - a) + square * a;
    }

public:
    void reset()
    {
        inputHp.reset();
        inputVoice.reset();
        modLowPass.reset();
        outputHp.reset();
        outputLowPass.reset();
        carrierPhase = lfoPhase = 0.0f;
        carrierHz = pitchCarrierHz();
        ampEnv = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setPitch1(float v)
    {
        pitch1 = clamp01(v);
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    float process(float in)
    {
        const float t = smoothstep(tone);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.06f + 1.02f * mix);

        float x = inputHp.process(in);
        x = inputVoice.process(x);
        ampEnv += onePoleCoeffHz(20.0f, sampleRate) * (std::fabs(x) - ampEnv);

        // MF-102 drive is not exposed by the game; keep it musical and let
        // Tone push a little more carrier bite.
        const float drive = 1.08f + 0.44f * t + 0.18f * m;
        const float pre = softClip(x * drive) * (0.96f - 0.06f * m);

        lfoPhase += (0.11f + 5.2f * t * t) / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);
        const float lfo = std::sin(kTwoPi * lfoPhase);
        const float lfoDepth = (0.015f + 0.115f * t) * (0.45f + 0.55f * m);
        const float targetHz = pitchCarrierHz() * std::pow(2.0f, lfo * lfoDepth);
        carrierHz += carrierSlewA * (targetHz - carrierHz);

        carrierPhase += carrierHz / sampleRate;
        if (carrierPhase >= 1.0f)
            carrierPhase -= std::floor(carrierPhase);

        const float carrier = carrierWave(carrierPhase);
        const float carrier2 = carrierWave(carrierPhase * 2.0f + 0.17f);

        float balanced = pre * carrier * (1.42f + 0.62f * t);
        balanced += pre * carrier2 * (0.10f + 0.16f * t);

        // Imperfect null and carrier bleed keep the analog MF-102 feel, but
        // stay low enough to avoid a volume jump at common the game Mix=50.
        const float signalLeak = pre * (0.030f + 0.025f * (1.0f - t));
        const float carrierLeak = carrier * (0.004f + 0.007f * t) * (0.45f + 0.55f * ampEnv);
        float wet = balanced + signalLeak + carrierLeak;
        wet = modLowPass.process(wet);
        wet = outputHp.process(softClip(wet * (1.02f + 0.18f * t)));
        wet = outputLowPass.process(wet);

        const float dryLevel = 1.0f - 0.66f * m;
        const float wetLevel = (0.28f + 0.82f * m) * m;
        return softClip(in * dryLevel + wet * wetLevel) * 0.98f;
    }
};

class MultiPitchPlugin : public Plugin
{
    MultiPitchCore left;
    MultiPitchCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setPitch1(params[kPitch1]);
        right.setPitch1(params[kPitch1]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    MultiPitchPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMultiPitchDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MultiPitch"; }
    const char* getDescription() const override { return "MF-102 style ring mod pitch sidebands"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 't', 'P', 'i'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMultiPitchNames[index];
        parameter.symbol = kMultiPitchSymbols[index];
        parameter.ranges.min = kMultiPitchMin[index];
        parameter.ranges.max = kMultiPitchMax[index];
        parameter.ranges.def = kMultiPitchDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiPitchPlugin)
};

Plugin* createPlugin()
{
    return new MultiPitchPlugin();
}

END_NAMESPACE_DISTRHO
