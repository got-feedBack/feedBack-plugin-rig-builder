/*
 * BZ-1 - Chief-style silicon fuzz for the game's Pedal_FuzzWasHe.
 * The local reference is a JFET-input silicon transistor fuzz with a
 * muff-style tone stack and output buffer. the game exposes only Gain and
 * Tone, so the DSP keeps those character controls and fixes output level
 * internally.
 */
#include "DistrhoPlugin.hpp"
#include "BZ1Params.h"
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

static inline float siliconClip(float x, float posDrive, float negDrive)
{
    const float y = x >= 0.0f ? softClip(x * posDrive) : softClip(x * negDrive);
    return 0.92f * y + 0.08f * softClip(x * 0.55f);
}

} // namespace

class BZ1Core
{
    float sampleRate = 48000.0f;
    float gain = kBZ1Def[kGain];
    float tone = kBZ1Def[kTone];

    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float inputLowY = 0.0f;
    float toneLowY = 0.0f;
    float toneHighY = 0.0f;
    float outputLowY = 0.0f;
    float compEnv = 0.0f;

    float hpA = 0.0f;
    float inputLowA = 0.0f;
    float toneLowA = 0.0f;
    float toneHighA = 0.0f;
    float outputLowA = 0.0f;
    float compAttackA = 0.0f;
    float compReleaseA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;

        const float hpHz = 30.0f + 55.0f * gain;
        const float hpRc = 1.0f / (2.0f * 3.14159265359f * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float inputLowHz = 5200.0f - 1300.0f * gain;
        inputLowA = 1.0f - std::exp(-2.0f * 3.14159265359f * inputLowHz / sampleRate);

        const float lowHz = 420.0f + 420.0f * tone;
        toneLowA = 1.0f - std::exp(-2.0f * 3.14159265359f * lowHz / sampleRate);

        const float highHz = 950.0f + 1800.0f * tone;
        toneHighA = 1.0f - std::exp(-2.0f * 3.14159265359f * highHz / sampleRate);

        const float outputLowHz = 7600.0f + 3800.0f * tone;
        outputLowA = 1.0f - std::exp(-2.0f * 3.14159265359f * outputLowHz / sampleRate);

        compAttackA = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
        compReleaseA = 1.0f - std::exp(-1.0f / (0.090f * sampleRate));
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float onePoleLow(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

    void updateCompression(float x)
    {
        const float target = clamp01(std::fabs(x) * 1.15f);
        const float a = target > compEnv ? compAttackA : compReleaseA;
        compEnv += a * (target - compEnv);
    }

public:
    void reset()
    {
        hpX1 = hpY1 = inputLowY = toneLowY = toneHighY = outputLowY = compEnv = 0.0f;
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
        const float fuzz = 0.06f + 0.94f * gain;

        // Input buffer and coupling caps: tight lows, gentle transistor edge.
        float x = highPass(in);
        x = onePoleLow(x, inputLowY, inputLowA);
        x *= 1.35f + 2.65f * fuzz;
        x = 0.72f * x + 0.28f * siliconClip(x - 0.018f, 1.35f, 1.05f);

        // Pre-gain stage. This is smoother than Buzz-Tone: silicon bite,
        // but not the same starved 1.5 V splat.
        float y = siliconClip((x * (2.1f + 5.7f * fuzz)) - (0.035f + 0.025f * fuzz),
                              1.25f + 1.75f * fuzz,
                              0.92f + 1.15f * fuzz);

        // Main FZ-3/Fuzz Face-like middle transistor gain stage. Gain raises
        // sustain and compression more than output volume.
        updateCompression(y);
        const float sustainTrim = 1.0f - 0.26f * compEnv;
        y = siliconClip((y * (3.2f + 15.5f * fuzz * fuzz) * sustainTrim) + 0.055f,
                        1.55f + 2.65f * fuzz,
                        1.05f + 1.80f * fuzz);

        // Recovery/output transistor before the passive tone network.
        y = siliconClip((y * (1.8f + 2.9f * fuzz)) - 0.018f,
                        1.18f + 1.20f * fuzz,
                        1.00f + 1.00f * fuzz);

        // Big Muff-style balance: dark side is bass-heavy, bright side keeps
        // sharper clipped harmonics. Center is close to flat but slightly
        // scooped, matching this family of tone stacks.
        const float low = onePoleLow(y, toneLowY, toneLowA);
        const float highBase = onePoleLow(y, toneHighY, toneHighA);
        const float high = y - 0.72f * highBase;
        y = low * (1.03f - 0.78f * tone)
          + high * (0.22f + 1.08f * tone)
          + y * 0.18f;

        y = onePoleLow(y, outputLowY, outputLowA);

        const float levelTrim = 0.30f / (1.0f + 0.12f * fuzz);
        return levelTrim * softClip(y * (1.40f + 0.45f * fuzz));
    }
};

class BZ1Plugin : public Plugin
{
    BZ1Core left;
    BZ1Core right;
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
    BZ1Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBZ1Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BZ-1"; }
    const char* getDescription() const override { return "Chief-style silicon fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', '0', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBZ1Names[index];
        parameter.symbol = kBZ1Symbols[index];
        parameter.ranges.min = kBZ1Min[index];
        parameter.ranges.max = kBZ1Max[index];
        parameter.ranges.def = kBZ1Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BZ1Plugin)
};

Plugin* createPlugin()
{
    return new BZ1Plugin();
}

END_NAMESPACE_DISTRHO
