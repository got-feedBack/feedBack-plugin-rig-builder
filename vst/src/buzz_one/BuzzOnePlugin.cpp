/*
 * BuzzOne - Univox Super-Fuzz style pedal for Rocksmith's Pedal_BuzzOne.
 * The local schematic shows the classic 2SC828 preamp, phase-split full-wave
 * octave fuzz, OA90 diode clipping, and passive tone/balance network. Rocksmith
 * exposes Gain and Tone, so this DSP keeps those two controls and fixes level.
 */
#include "DistrhoPlugin.hpp"
#include "BuzzOneParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float diodeClip(float x, float drive)
{
    const float y = softClip(x * drive);
    return 0.84f * y + 0.16f * softClip(x * 0.48f);
}

static inline float asymTransistor(float x, float posDrive, float negDrive, float bias)
{
    x += bias;
    return x >= 0.0f ? softClip(x * posDrive) : softClip(x * negDrive);
}

} // namespace

class BuzzOneCore
{
    float sampleRate = 48000.0f;
    float gain = kBuzzOneDef[kGain];
    float tone = kBuzzOneDef[kTone];

    float inHpX1 = 0.0f;
    float inHpY1 = 0.0f;
    float dcX1 = 0.0f;
    float dcY1 = 0.0f;
    float outputHpX1 = 0.0f;
    float outputHpY1 = 0.0f;
    float preLowY = 0.0f;
    float toneLowY = 0.0f;
    float toneHighY = 0.0f;
    float toneMidY = 0.0f;
    float topY = 0.0f;
    float compEnv = 0.0f;

    float inHpA = 0.0f;
    float dcA = 0.0f;
    float outputHpA = 0.0f;
    float preLowA = 0.0f;
    float toneLowA = 0.0f;
    float toneHighA = 0.0f;
    float toneMidA = 0.0f;
    float topA = 0.0f;
    float compAttackA = 0.0f;
    float compReleaseA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;

        const float inHpHz = 32.0f + 46.0f * gain;
        const float inHpRc = 1.0f / (2.0f * kPi * inHpHz);
        inHpA = inHpRc / (inHpRc + dt);

        const float dcHz = 18.0f;
        const float dcRc = 1.0f / (2.0f * kPi * dcHz);
        dcA = dcRc / (dcRc + dt);

        const float outputHpHz = 38.0f + 52.0f * (1.0f - tone);
        const float outputHpRc = 1.0f / (2.0f * kPi * outputHpHz);
        outputHpA = outputHpRc / (outputHpRc + dt);

        preLowA = 1.0f - std::exp(-2.0f * kPi * (7200.0f - 1700.0f * gain) / sampleRate);
        toneLowA = 1.0f - std::exp(-2.0f * kPi * (310.0f + 250.0f * tone) / sampleRate);
        toneHighA = 1.0f - std::exp(-2.0f * kPi * (760.0f + 1650.0f * tone) / sampleRate);
        toneMidA = 1.0f - std::exp(-2.0f * kPi * (1120.0f + 720.0f * tone) / sampleRate);
        topA = 1.0f - std::exp(-2.0f * kPi * (5200.0f + 5400.0f * tone) / sampleRate);

        compAttackA = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
        compReleaseA = 1.0f - std::exp(-1.0f / (0.080f * sampleRate));
    }

    float highPass(float x, float& x1, float& y1, float a)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }

    float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

    void updateCompression(float x)
    {
        const float target = clamp01(std::fabs(x) * 1.35f);
        const float a = target > compEnv ? compAttackA : compReleaseA;
        compEnv += a * (target - compEnv);
    }

public:
    void reset()
    {
        inHpX1 = inHpY1 = dcX1 = dcY1 = outputHpX1 = outputHpY1 = 0.0f;
        preLowY = toneLowY = toneHighY = toneMidY = topY = compEnv = 0.0f;
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
        const float fuzz = 0.08f + 0.92f * gain;

        float x = highPass(in, inHpX1, inHpY1, inHpA);
        x = lowPass(x, preLowY, preLowA);

        // Q1/Q2 preamp gain into the expander area of the schematic.
        x = asymTransistor(x * (1.65f + 4.60f * fuzz), 1.25f + 1.45f * fuzz,
                           1.05f + 1.10f * fuzz, -0.018f - 0.020f * fuzz);

        // Super-Fuzz core: phase-split pair feeding full-wave/octave content,
        // then the OA90 diode clipping node. The octave blend rises with Gain.
        const float splitterDrive = 2.15f + 6.90f * fuzz;
        const float upper = asymTransistor(x * splitterDrive, 1.50f + 1.35f * fuzz,
                                           1.00f + 0.95f * fuzz, 0.020f);
        const float lower = asymTransistor(-x * splitterDrive, 1.42f + 1.20f * fuzz,
                                           0.98f + 0.90f * fuzz, 0.018f);
        const float fundamental = upper - lower;
        const float octave = upper + lower;

        float y = fundamental * (0.42f - 0.10f * fuzz)
                + octave * (0.56f + 1.10f * fuzz);
        y = highPass(y, dcX1, dcY1, dcA);

        updateCompression(y);
        const float squash = 1.0f - 0.22f * compEnv;
        y = diodeClip((y * (2.00f + 4.20f * fuzz) * squash) - 0.030f,
                      1.95f + 2.50f * fuzz);
        y = diodeClip((y * (1.25f + 2.40f * fuzz)) + 0.018f,
                      1.65f + 1.70f * fuzz);

        // Continuous version of the Super-Fuzz tone switch: left is thick and
        // nasal; right is the scooped bright buzz. Center keeps both voices.
        const float low = lowPass(y, toneLowY, toneLowA);
        const float highBase = lowPass(y, toneHighY, toneHighA);
        const float high = y - 0.74f * highBase;
        const float mid = lowPass(highBase - low, toneMidY, toneMidA);
        const float scoop = y - mid * (0.40f + 0.55f * tone);

        y = low * (1.02f - 0.72f * tone)
          + high * (0.20f + 1.18f * tone)
          + scoop * (0.18f + 0.42f * tone);

        y = lowPass(y, topY, topA);
        y = highPass(y, outputHpX1, outputHpY1, outputHpA);

        const float levelTrim = 0.27f / (1.0f + 0.12f * fuzz);
        return levelTrim * softClip(y * (1.65f + 0.50f * fuzz));
    }
};

class BuzzOnePlugin : public Plugin
{
    BuzzOneCore left;
    BuzzOneCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    BuzzOnePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBuzzOneDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BuzzOne"; }
    const char* getDescription() const override { return "Super-Fuzz style octave fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'z', 'O', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBuzzOneNames[index];
        parameter.symbol = kBuzzOneSymbols[index];
        parameter.ranges.min = kBuzzOneMin[index];
        parameter.ranges.max = kBuzzOneMax[index];
        parameter.ranges.def = kBuzzOneDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BuzzOnePlugin)
};

Plugin* createPlugin()
{
    return new BuzzOnePlugin();
}

END_NAMESPACE_DISTRHO
