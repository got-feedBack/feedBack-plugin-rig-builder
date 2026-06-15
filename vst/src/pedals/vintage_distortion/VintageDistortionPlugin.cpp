/*
 * VintageDistortion - DOD 250 style distortion for the game's
 * Pedal_VintageDistortion.
 *
 * Local reference: pedals/vintage distortion.png. The schematic is a DOD 250
 * Overdrive/Preamp: LM741 op-amp gain stage, small feedback cap, passive
 * output filtering and asymmetric 1N4148 diode clipping. the game exposes
 * Gain and Tone, so output level is internally normalized.
 */
#include "DistrhoPlugin.hpp"
#include "VintageDistortionParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(20.0f, std::fmin(hz, nyquist));
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float diodeClip250(float x, float gain)
{
    // Asymmetric DOD-250-ish clipping: one diode one way, two in series the
    // other way. The thresholds are softened to avoid hard digital edges.
    const float posThresh = 0.42f - 0.08f * gain;
    const float negThresh = 0.78f - 0.10f * gain;
    if (x >= 0.0f)
        return posThresh * std::tanh(x / posThresh);
    return -negThresh * std::tanh((-x) / negThresh);
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
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

} // namespace

class VintageDistortionCore
{
    float sampleRate = 48000.0f;
    float gain = kVintageDistortionDef[kGain];
    float tone = kVintageDistortionDef[kTone];

    Biquad inputHp;
    Biquad opAmpVoice;
    Biquad opAmpRollOff;
    Biquad postMid;
    Biquad toneShelf;
    Biquad toneLowPass;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 58.0f + 120.0f * g, 0.70f);
        opAmpVoice.setPeaking(sampleRate, 760.0f + 420.0f * tone, 0.82f,
                              1.0f + 4.0f * g);
        // LM741 feedback cap / limited slew cue: more gain loses some top.
        opAmpRollOff.setLowPass(sampleRate, 8200.0f - 2300.0f * g, 0.72f);
        postMid.setPeaking(sampleRate, 1050.0f, 0.70f,
                           1.8f - 2.7f * tone + 1.2f * g);
        toneShelf.setHighShelf(sampleRate, 2200.0f + 1800.0f * tone, 0.74f,
                               -7.0f + 13.0f * tone);
        toneLowPass.setLowPass(sampleRate, 2600.0f + 7200.0f * tone, 0.64f);
    }

public:
    void reset()
    {
        inputHp.reset();
        opAmpVoice.reset();
        opAmpRollOff.reset();
        postMid.reset();
        toneShelf.reset();
        toneLowPass.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateFilters();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float g = smoothstep(gain);
        float x = inputHp.process(in);
        x = opAmpVoice.process(x);

        // Gain pot around the op-amp: enough fixed push that low RS Gain still
        // has pedal color, but high Gain reaches clear vintage distortion.
        const float drive = 2.2f + 9.0f * gain + 24.0f * g;
        float y = x * drive;
        y = opAmpRollOff.process(y);

        const float clipped = diodeClip250(y + 0.025f * gain, gain);
        const float opAmpSat = softClip(y * (0.22f + 0.42f * gain)) * 0.36f;
        y = clipped * (0.86f + 0.10f * gain) + opAmpSat;

        // The real DOD has no blend; this tiny clean leak keeps RS Gain 11/25
        // usable without making higher presets sound like a boost.
        const float cleanLeak = 0.10f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = postMid.process(y);
        y = toneShelf.process(y);
        y = toneLowPass.process(y);

        // No the game output knob. Trim high gain and slightly lift low-gain
        // presets so engaging the pedal feels like distortion, not a level jump.
        const float level = 0.72f / (1.0f + 0.50f * gain);
        return softClip(y * level) * 0.98f;
    }
};

class VintageDistortionPlugin : public Plugin
{
    VintageDistortionCore left;
    VintageDistortionCore right;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    VintageDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kVintageDistortionDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "VintageDistortion"; }
    const char* getDescription() const override { return "DOD 250 style vintage distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 'i', 'D', 's'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kVintageDistortionNames[index];
        parameter.symbol = kVintageDistortionSymbols[index];
        parameter.ranges.min = kVintageDistortionMin[index];
        parameter.ranges.max = kVintageDistortionMax[index];
        parameter.ranges.def = kVintageDistortionDef[index];
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
        makeupL.snap();
        makeupR.snap();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        makeupL.setSampleRate((float)newSampleRate);
        makeupR.setSampleRate((float)newSampleRate);
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
            // Auto makeup-gain: match output loudness to the dry input so the
            // drive's controls change only the amount of clip, not the level.
            outL[i] = makeupL.process(inL[i], left.process(inL[i]));
            outR[i] = makeupR.process(inR[i], right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageDistortionPlugin)
};

Plugin* createPlugin()
{
    return new VintageDistortionPlugin();
}

END_NAMESPACE_DISTRHO
