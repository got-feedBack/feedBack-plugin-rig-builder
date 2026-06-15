/*
 * Buzz-Tone - low-headroom germanium fuzz for the game's
 * Pedal_CaptFuzzle. The reference schematic is a three-2N1305 circuit running
 * from 1.5 V with fixed volume, a fuzz bias network, and small coupling caps.
 * the game exposes only Gain and Tone, so this DSP keeps the audible behavior:
 * narrow coupling, asymmetric germanium clipping, starved-supply compression,
 * and a post-fuzz brightness control.
 */
#include "DistrhoPlugin.hpp"
#include "BuzzToneParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float asymClip(float x, float posDrive, float negDrive)
{
    return x >= 0.0f ? softClip(x * posDrive) : softClip(x * negDrive);
}

} // namespace

class BuzzToneCore
{
    float sampleRate = 48000.0f;
    float gain = kBuzzToneDef[kGain];
    float tone = kBuzzToneDef[kTone];

    float inHpX1 = 0.0f;
    float inHpY1 = 0.0f;
    float outHpX1 = 0.0f;
    float outHpY1 = 0.0f;
    float toneY = 0.0f;
    float sagEnv = 0.0f;

    float inHpA = 0.0f;
    float outHpA = 0.0f;
    float toneA = 0.0f;
    float sagAttackA = 0.0f;
    float sagReleaseA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;

        const float inHpHz = 25.0f + 55.0f * gain;
        const float inHpRc = 1.0f / (2.0f * 3.14159265359f * inHpHz);
        inHpA = inHpRc / (inHpRc + dt);

        const float outHpHz = 45.0f + 55.0f * (1.0f - tone);
        const float outHpRc = 1.0f / (2.0f * 3.14159265359f * outHpHz);
        outHpA = outHpRc / (outHpRc + dt);

        const float toneHz = 520.0f * std::pow(11.0f, tone);
        const float toneX = std::exp(-2.0f * 3.14159265359f * toneHz / sampleRate);
        toneA = 1.0f - toneX;

        sagAttackA = 1.0f - std::exp(-1.0f / (0.035f * sampleRate));
        sagReleaseA = 1.0f - std::exp(-1.0f / (0.045f * sampleRate));
    }

    float inputHighPass(float x)
    {
        const float y = inHpA * (inHpY1 + x - inHpX1);
        inHpX1 = x;
        inHpY1 = y;
        return y;
    }

    float outputHighPass(float x)
    {
        const float y = outHpA * (outHpY1 + x - outHpX1);
        outHpX1 = x;
        outHpY1 = y;
        return y;
    }

    float toneLowPass(float x)
    {
        toneY += toneA * (x - toneY);
        return toneY;
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 0.55f);
        const float a = target > sagEnv ? sagAttackA : sagReleaseA;
        sagEnv += a * (target - sagEnv);
    }

public:
    void reset()
    {
        inHpX1 = inHpY1 = outHpX1 = outHpY1 = toneY = sagEnv = 0.0f;
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
        const float fuzz = 0.10f + 0.90f * gain;
        float x = inputHighPass(in);

        // Q1 cue: small-cap input, boosted into a biased germanium stage.
        const float inputPush = 3.0f + 11.0f * fuzz * fuzz;
        x = asymClip((x * inputPush) - (0.030f + 0.030f * fuzz),
                     1.15f + 2.00f * fuzz,
                     0.82f + 1.30f * fuzz);

        // Q2 cue: the fuzz pot in the schematic shifts the bias/feedback area.
        // High Gain gets compressed and spitty instead of just louder.
        const float starvation = 1.0f - 0.07f * clamp01(sagEnv);
        float y = x * (3.4f + 17.5f * fuzz) * starvation;
        y -= 0.055f + 0.030f * fuzz;
        y = asymClip(y, 1.75f + 2.90f * fuzz, 0.72f + 1.65f * fuzz);
        const float q2 = y;

        // Q3 cue: fixed recovery/output transistor with more low-voltage
        // flattening. This is the main "Buzz-Tone" splat.
        updateSag(y);
        const float sagged = 1.0f - 0.10f * clamp01(sagEnv);
        y = y * (2.0f + 7.5f * fuzz) * sagged + 0.035f;
        y = asymClip(y, 1.40f + 2.20f * fuzz, 0.95f + 1.90f * fuzz);
        y = y * (0.88f + 0.12f * sagged) + q2 * (0.090f + 0.060f * fuzz) + x * 0.026f;

        // The real low-voltage circuit has more sustain than a literal gatey
        // starve model. Lift the note tail when the envelope relaxes, but keep
        // the first hit controlled so this still feels like a fuzz, not a boost.
        const float tailLift = 1.0f + 0.78f * fuzz * (1.0f - clamp01(sagEnv * 1.85f));
        y *= tailLift;

        // The 0.01 uF output cap into the volume control thins the fuzz before
        // the tone control. the game has no Level knob, so output stays fixed.
        y = outputHighPass(y);

        const float dark = toneLowPass(y);
        y = dark * (0.90f - 0.35f * tone) + y * (0.25f + 0.85f * tone);

        const float levelTrim = 0.32f / (1.0f + 0.18f * fuzz);
        return levelTrim * softClip(y * (1.45f + 0.60f * fuzz));
    }
};

class BuzzTonePlugin : public Plugin
{
    BuzzToneCore left;
    BuzzToneCore right;
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
    BuzzTonePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBuzzToneDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Buzz-Tone"; }
    const char* getDescription() const override { return "Low-headroom germanium fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 3); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 't', 'n'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBuzzToneNames[index];
        parameter.symbol = kBuzzToneSymbols[index];
        parameter.ranges.min = kBuzzToneMin[index];
        parameter.ranges.max = kBuzzToneMax[index];
        parameter.ranges.def = kBuzzToneDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BuzzTonePlugin)
};

Plugin* createPlugin()
{
    return new BuzzTonePlugin();
}

END_NAMESPACE_DISTRHO
