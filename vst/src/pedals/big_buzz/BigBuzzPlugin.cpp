/*
 * Big Buzz - triangle-era four-transistor fuzz for the game's Pedal_BuzzToo.
 *
 * Local reference: pedals/buzz 2.jpg. The schematic has four NPN transistor
 * stages, two silicon diode clipping stages, a passive bass/treble tone stack,
 * and output volume. the game exposes Gain and Tone only, so output is
 * level-compensated internally.
 */
#include "DistrhoPlugin.hpp"
#include "BigBuzzParams.h"
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
    return std::fmax(18.0f, std::fmin(hz, nyquist));
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

static inline float diodeClip(float x, float threshold)
{
    threshold = std::fmax(0.05f, threshold);
    return threshold * std::tanh(x / threshold);
}

static inline float transistorStage(float x, float drive, float bias)
{
    x = x * drive + bias;
    const float pos = softClip(x * 1.10f);
    const float neg = softClip(x * 0.88f);
    return x >= 0.0f ? pos : neg;
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
};

} // namespace

class BigBuzzCore
{
    float sampleRate = 48000.0f;
    float gain = kBigBuzzDef[kGain];
    float tone = kBigBuzzDef[kTone];

    Biquad inputHp;
    Biquad inputVoice;
    Biquad clipOneLowPass;
    Biquad clipTwoLowPass;
    Biquad toneLow;
    Biquad toneHighCut;
    Biquad toneScoop;
    Biquad outputHp;
    Biquad outputLowPass;

    float sagEnv = 0.0f;
    float sagAttackA = 0.0f;
    float sagReleaseA = 0.0f;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 42.0f + 72.0f * g, 0.68f);
        inputVoice.setPeaking(sampleRate, 620.0f + 190.0f * tone, 0.72f,
                              1.8f + 3.2f * g);

        // The 500 pF feedback caps shave the fizz harder as Sustain rises.
        clipOneLowPass.setLowPass(sampleRate, 6400.0f - 1500.0f * g, 0.70f);
        clipTwoLowPass.setLowPass(sampleRate, 5600.0f - 1350.0f * g, 0.68f);

        // Passive two-branch tone stack approximation: low side around C8/R8,
        // high side around C9/R5. Tone crossfades those lossy branches.
        toneLow.setLowPass(sampleRate, 360.0f + 230.0f * tone, 0.66f);
        toneHighCut.setLowPass(sampleRate, 760.0f + 5200.0f * tone, 0.62f);
        toneScoop.setPeaking(sampleRate, 820.0f + 260.0f * tone, 0.58f,
                             -6.5f - 7.0f * tone);

        outputHp.setHighPass(sampleRate, 54.0f + 38.0f * (1.0f - tone), 0.67f);
        outputLowPass.setLowPass(sampleRate, 5200.0f + 6500.0f * tone, 0.62f);

        sagAttackA = 1.0f - std::exp(-1.0f / (0.010f * sampleRate));
        sagReleaseA = 1.0f - std::exp(-1.0f / (0.135f * sampleRate));
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 1.8f);
        const float a = target > sagEnv ? sagAttackA : sagReleaseA;
        sagEnv += a * (target - sagEnv);
    }

public:
    void reset()
    {
        inputHp.reset();
        inputVoice.reset();
        clipOneLowPass.reset();
        clipTwoLowPass.reset();
        toneLow.reset();
        toneHighCut.reset();
        toneScoop.reset();
        outputHp.reset();
        outputLowPass.reset();
        sagEnv = 0.0f;
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
        const float sustain = 0.06f + 0.94f * gain;

        float x = inputHp.process(in);
        x = inputVoice.process(x);

        // Q4 input booster and Sustain pot into the first clipping transistor.
        x = transistorStage(x, 2.0f + 5.0f * sustain, -0.020f);
        x *= 1.05f + 7.8f * sustain + 9.5f * g;

        updateSag(x);
        const float sag = 1.0f - 0.20f * sagEnv * (0.45f + 0.55f * g);

        // Q3 and Q2: two cascaded silicon diode clip stages. The second stage
        // is slightly tighter, which gives the V1 Muff its compressed sustain.
        float y = clipOneLowPass.process(x * sag);
        y = diodeClip(y + 0.018f, 0.42f - 0.10f * g);
        y = transistorStage(y, 1.8f + 2.7f * sustain, 0.012f);

        y = clipTwoLowPass.process(y * (1.7f + 4.8f * sustain));
        y = diodeClip(y - 0.012f, 0.38f - 0.085f * g);
        y = transistorStage(y, 1.35f + 1.45f * sustain, -0.010f);

        // Two-branch tone stack: low Tone keeps the bass branch, high Tone opens
        // the treble branch and deepens the middle scoop.
        const float low = toneLow.process(y);
        const float highBase = toneHighCut.process(y);
        const float high = y - 0.72f * highBase;
        y = low * (1.28f - 0.92f * tone)
          + high * (0.24f + 1.42f * tone)
          + y * (0.12f + 0.10f * (1.0f - tone));
        y = toneScoop.process(y);

        // Q1 recovery/volume stage. There is no RS volume knob, so compensate
        // high sustain and slightly lift very low Gain presets.
        y = transistorStage(y, 1.35f + 0.95f * tone, 0.006f);
        y = outputHp.process(y);
        y = outputLowPass.process(y);

        const float lowGainLift = 1.0f + 0.38f * (1.0f - gain);
        const float level = (0.48f * lowGainLift) / (1.0f + 0.62f * g);
        return softClip(y * level) * 0.98f;
    }
};

class BigBuzzPlugin : public Plugin
{
    BigBuzzCore left;
    BigBuzzCore right;
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
    BigBuzzPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBigBuzzDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Big Buzz"; }
    const char* getDescription() const override { return "Triangle-era four-transistor fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 'T', '2'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBigBuzzNames[index];
        parameter.symbol = kBigBuzzSymbols[index];
        parameter.ranges.min = kBigBuzzMin[index];
        parameter.ranges.max = kBigBuzzMax[index];
        parameter.ranges.def = kBigBuzzDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigBuzzPlugin)
};

Plugin* createPlugin()
{
    return new BigBuzzPlugin();
}

END_NAMESPACE_DISTRHO
