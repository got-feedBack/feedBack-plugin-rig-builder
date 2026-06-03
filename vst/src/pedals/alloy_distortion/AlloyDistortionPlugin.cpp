/*
 * AlloyDistortion - Boss HM-2 style metal distortion for Rocksmith's
 * Pedal_MetalDistortion.
 *
 * Local reference: pedals/alloy distortion.pdf. The HM-2 has asymmetric soft
 * clipping, hard clipping, germanium crossover artifacts, and separate low/high
 * color controls. Rocksmith exposes Gain and Tone only, so Tone is a combined
 * color blend and output level is internally compensated.
 */
#include "DistrhoPlugin.hpp"
#include "AlloyDistortionParams.h"
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

static inline float asym(float x, float posDrive, float negDrive)
{
    return x >= 0.0f ? softClip(x * posDrive) : softClip(x * negDrive);
}

static inline float crossoverGe(float x, float amount)
{
    const float dead = 0.018f + 0.020f * amount;
    if (std::fabs(x) < dead)
        return x * (0.18f + 0.20f * (1.0f - amount));
    return (x > 0.0f ? x - dead : x + dead);
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

class AlloyDistortionCore
{
    float sampleRate = 48000.0f;
    float gain = kAlloyDistortionDef[kGain];
    float tone = kAlloyDistortionDef[kTone];

    Biquad inputHp;
    Biquad lowColor;
    Biquad upperColor;
    Biquad clipRollOff;
    Biquad chainsawMid;
    Biquad toneShelf;
    Biquad outputLp;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 70.0f + 80.0f * gain, 0.68f);
        lowColor.setPeaking(sampleRate, 118.0f + 80.0f * tone, 0.72f,
                            1.0f + 7.0f * (1.0f - tone));
        upperColor.setPeaking(sampleRate, 920.0f + 520.0f * tone, 0.62f,
                              2.8f + 6.0f * tone + 2.0f * g);
        clipRollOff.setLowPass(sampleRate, 5400.0f - 1700.0f * g + 1300.0f * tone, 0.66f);
        chainsawMid.setPeaking(sampleRate, 1050.0f + 500.0f * tone, 0.55f,
                               3.0f + 5.2f * tone);
        toneShelf.setHighShelf(sampleRate, 2100.0f + 1800.0f * tone, 0.70f,
                               -7.0f + 13.5f * tone);
        outputLp.setLowPass(sampleRate, 2700.0f + 7000.0f * tone, 0.62f);
    }

public:
    void reset()
    {
        inputHp.reset();
        lowColor.reset();
        upperColor.reset();
        clipRollOff.reset();
        chainsawMid.reset();
        toneShelf.reset();
        outputLp.reset();
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
        x = lowColor.process(x);
        x = upperColor.process(x);

        const float drive = 1.8f + 8.0f * gain + 22.0f * g;
        float y = asym(x * drive, 0.80f + 1.25f * gain, 0.55f + 1.65f * gain);
        y = crossoverGe(y, 0.45f + 0.55f * gain);
        y = softClip(y * (1.35f + 2.8f * gain));
        y = clipRollOff.process(y);

        const float hard = softClip(y * (2.2f + 3.0f * gain)) * 0.32f;
        y = y * 0.78f + hard;

        y = chainsawMid.process(y);
        y = toneShelf.process(y);
        y = outputLp.process(y);

        const float cleanLeak = 0.08f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        const float level = 0.66f / (1.0f + 0.46f * gain + 0.28f * g);
        return softClip(y * level) * 0.98f;
    }
};

class AlloyDistortionPlugin : public Plugin
{
    AlloyDistortionCore left;
    AlloyDistortionCore right;
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
    AlloyDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAlloyDistortionDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AlloyDistortion"; }
    const char* getDescription() const override { return "HM-2 style alloy metal distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'l', 'D', 'y'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAlloyDistortionNames[index];
        parameter.symbol = kAlloyDistortionSymbols[index];
        parameter.ranges.min = kAlloyDistortionMin[index];
        parameter.ranges.max = kAlloyDistortionMax[index];
        parameter.ranges.def = kAlloyDistortionDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyDistortionPlugin)
};

Plugin* createPlugin()
{
    return new AlloyDistortionPlugin();
}

END_NAMESPACE_DISTRHO
