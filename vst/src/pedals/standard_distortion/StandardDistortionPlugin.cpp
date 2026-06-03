/*
 * StandardDistortion - Boss DS-1 style distortion for Rocksmith's
 * Pedal_Distortion.
 *
 * Local reference: pedals/standard distortion.pdf. The DS-1 uses transistor
 * buffers, a high-gain TA7136-style stage, hard silicon diode clipping, and a
 * post-clipping tone network. Rocksmith exposes only Gain and Tone, so Level
 * is internally compensated.
 */
#include "DistrhoPlugin.hpp"
#include "StandardDistortionParams.h"
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

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float hardDiodeClip(float x, float threshold)
{
    return threshold * std::tanh(x / threshold);
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

class StandardDistortionCore
{
    float sampleRate = 48000.0f;
    float gain = kStandardDistortionDef[kGain];
    float tone = kStandardDistortionDef[kTone];

    Biquad inputHp;
    Biquad preEmphasis;
    Biquad clipLowPass;
    Biquad toneScoop;
    Biquad toneShelf;
    Biquad toneLowPass;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 86.0f + 95.0f * gain, 0.68f);
        preEmphasis.setPeaking(sampleRate, 1250.0f + 420.0f * tone, 0.78f,
                               2.0f + 4.4f * g);
        clipLowPass.setLowPass(sampleRate, 6900.0f - 2200.0f * g, 0.66f);
        // DS-1 tone stack cue: scooped mids plus a bright top end when Tone is high.
        toneScoop.setPeaking(sampleRate, 760.0f + 260.0f * tone, 0.58f,
                             -3.8f - 3.8f * tone);
        toneShelf.setHighShelf(sampleRate, 1800.0f + 2400.0f * tone, 0.74f,
                               -9.0f + 18.0f * tone);
        toneLowPass.setLowPass(sampleRate, 2500.0f + 9000.0f * tone, 0.62f);
    }

public:
    void reset()
    {
        inputHp.reset();
        preEmphasis.reset();
        clipLowPass.reset();
        toneScoop.reset();
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
        x = preEmphasis.process(x);

        // DS-1-style high gain into hard clipping. Keep a little distortion at
        // low Rocksmith Gain without making Gain=7 behave like a full metal box.
        const float drive = 1.55f + 7.0f * gain + 24.0f * g;
        float y = x * drive;
        y = clipLowPass.process(y);

        const float threshold = 0.46f - 0.12f * gain;
        const float clipped = hardDiodeClip(y, threshold);
        const float icHair = softClip(y * (0.20f + 0.34f * gain)) * 0.18f;
        y = clipped + icHair;

        const float cleanLeak = 0.10f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = toneScoop.process(y);
        y = toneShelf.process(y);
        y = toneLowPass.process(y);

        // No Rocksmith Level knob. DS-1 can be very loud after clipping, so
        // normalize high Gain without turning low-gain presets into silence.
        const float level = 0.70f / (1.0f + 0.42f * gain + 0.32f * g);
        return softClip(y * level) * 0.98f;
    }
};

class StandardDistortionPlugin : public Plugin
{
    StandardDistortionCore left;
    StandardDistortionCore right;
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
    StandardDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStandardDistortionDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StandardDistortion"; }
    const char* getDescription() const override { return "DS-1 style standard distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 's', '1', 'D'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kStandardDistortionNames[index];
        parameter.symbol = kStandardDistortionSymbols[index];
        parameter.ranges.min = kStandardDistortionMin[index];
        parameter.ranges.max = kStandardDistortionMax[index];
        parameter.ranges.def = kStandardDistortionDef[index];
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
            // Auto makeup-gain: match output loudness to the dry input so Gain/
            // Tone change only the amount of clip, not the level (RB_AUTOMAKEUP).
            outL[i] = makeupL.process(inL[i], left.process(inL[i]));
            outR[i] = makeupR.process(inR[i], right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandardDistortionPlugin)
};

Plugin* createPlugin()
{
    return new StandardDistortionPlugin();
}

END_NAMESPACE_DISTRHO
