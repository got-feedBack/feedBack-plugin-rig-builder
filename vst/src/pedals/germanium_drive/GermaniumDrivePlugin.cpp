/*
 * GermaniumDrive - smooth germanium overdrive for Rocksmith's
 * Pedal_GermaniumDrive. This is not a full Hudson Broadcast/Skywave clone:
 * Rocksmith exposes only Gain and Tone, so the DSP keeps the circuit cues that
 * matter for that pedal slot: fixed input low cut, asymmetric germanium-style
 * saturation, a subtle transformer-like final softener, and a post tone filter.
 */
#include "DistrhoPlugin.hpp"
#include "GermaniumDriveParams.h"
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

} // namespace

class GermaniumDriveCore
{
    float sampleRate = 48000.0f;
    float gain = kGermaniumDriveDef[kGain];
    float tone = kGermaniumDriveDef[kTone];

    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float toneY = 0.0f;

    float hpA = 0.0f;
    float toneA = 0.0f;

    void updateFilters()
    {
        const float hpHz = 45.0f + 95.0f * gain;
        const float hpRc = 1.0f / (2.0f * 3.14159265359f * hpHz);
        const float dt = 1.0f / sampleRate;
        hpA = hpRc / (hpRc + dt);

        const float toneHz = 850.0f * std::pow(9.0f, tone);
        const float toneX = std::exp(-2.0f * 3.14159265359f * toneHz / sampleRate);
        toneA = 1.0f - toneX;
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

    float germaniumStage(float x) const
    {
        // Keep a small always-on germanium edge, but let the bottom of the
        // Gain control clean up instead of staying in obvious distortion.
        const float driveKnob = 0.08f + 0.92f * gain;
        const float drive = 2.2f + 42.0f * driveKnob * driveKnob;
        const float bias = -0.12f + 0.08f * gain;

        const float pushed = x * drive + bias;
        const float pos = softClip(pushed * (1.60f + 2.60f * driveKnob));
        const float neg = softClip(pushed * (0.65f + 1.20f * driveKnob));
        float y = pushed >= 0.0f ? pos : neg;

        // Leak only a little clean signal. The first revision kept too much
        // dry level at low/mid gain and sounded like a volume boost.
        const float cleanBlend = 0.08f * (1.0f - gain);
        y = y * (1.0f - cleanBlend) + x * cleanBlend;

        // Compensate the added internal push. Distortion should be audible,
        // but engaging this pedal should not feel like +10 dB of clean level.
        const float makeup = 0.38f / (1.0f + 0.75f * driveKnob);
        return y * makeup;
    }

public:
    void reset()
    {
        hpX1 = hpY1 = toneY = 0.0f;
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
        const float driveKnob = 0.08f + 0.92f * gain;
        const float hp = highPass(in) * (2.0f + 5.8f * driveKnob);

        // First stage: silicon-ish class-A preamp color, now intentionally
        // driven enough to feed the germanium stage from normal guitar DI.
        const float preGain = 1.8f + 12.0f * driveKnob;
        float y = 0.82f * softClip(hp * preGain * 0.60f) + 0.18f * hp;

        // Second stage: softer asymmetric germanium saturation.
        y = germaniumStage(y);

        // Tone is mostly a brightness control. Low values get the low-pass
        // result; high values restore more of the pre-filtered harmonics.
        const float dark = toneLowPass(y);
        y = dark * (1.0f - 0.55f * tone) + y * (0.55f * tone);

        // Output transformer cue: extra rounding after the two gain stages,
        // trimmed down so the perceived result is drive, not volume boost.
        y = 0.42f * softClip(y * (1.50f + 1.30f * driveKnob));
        return y;
    }
};

class GermaniumDrivePlugin : public Plugin
{
    GermaniumDriveCore left;
    GermaniumDriveCore right;
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
    GermaniumDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kGermaniumDriveDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "GermaniumDrive"; }
    const char* getDescription() const override { return "Classic smooth germanium overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 2); }
    int64_t getUniqueId() const override { return d_cconst('G', 'd', 'r', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kGermaniumDriveNames[index];
        parameter.symbol = kGermaniumDriveSymbols[index];
        parameter.ranges.min = kGermaniumDriveMin[index];
        parameter.ranges.max = kGermaniumDriveMax[index];
        parameter.ranges.def = kGermaniumDriveDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GermaniumDrivePlugin)
};

Plugin* createPlugin()
{
    return new GermaniumDrivePlugin();
}

END_NAMESPACE_DISTRHO
