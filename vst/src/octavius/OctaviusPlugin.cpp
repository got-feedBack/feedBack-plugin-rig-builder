/*
 * Octavius - Boss OC-2 style monophonic octave-down pedal for Rocksmith's
 * Pedal_Octavius.
 *
 * Local reference: pedals/octavius.pdf. The OC-2 uses detection and flip-flop
 * dividers for one and two octaves below the input. Rocksmith exposes Tone and
 * Mix, so Direct/OCT1/OCT2 levels are voiced internally.
 */
#include "DistrhoPlugin.hpp"
#include "OctaviusParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

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

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

} // namespace

class OctaviusCore
{
    float sampleRate = 48000.0f;
    float tone = kOctaviusDef[kTone];
    float mix = kOctaviusDef[kMix];

    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float detectY = 0.0f;
    float env = 0.0f;
    float gate = 0.0f;
    float sub1Y = 0.0f;
    float sub2Y = 0.0f;
    float toneY = 0.0f;
    float dryY = 0.0f;

    float hpA = 0.0f;
    float detectA = 0.0f;
    float envA = 0.0f;
    float sub1A = 0.0f;
    float sub2A = 0.0f;
    float toneA = 0.0f;
    float dryA = 0.0f;

    bool armed = true;
    bool div1 = false;
    bool div2 = false;
    int halfCycleCount = 0;
    int samplesSinceEdge = 0;
    int lastPeriod = 240;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 42.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float t = smoothstep(tone);
        detectA = onePoleCoeffHz(760.0f, sampleRate);
        envA = onePoleCoeffHz(34.0f, sampleRate);
        sub1A = onePoleCoeffHz(145.0f + 780.0f * t, sampleRate);
        sub2A = onePoleCoeffHz(92.0f + 420.0f * t, sampleRate);
        toneA = onePoleCoeffHz(720.0f + 4200.0f * t, sampleRate);
        dryA = onePoleCoeffHz(7800.0f, sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

public:
    void reset()
    {
        hpX1 = hpY1 = detectY = env = gate = sub1Y = sub2Y = toneY = dryY = 0.0f;
        armed = true;
        div1 = div2 = false;
        halfCycleCount = 0;
        samplesSinceEdge = 0;
        lastPeriod = (int)(sampleRate / 200.0f);
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
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
        float dry = lowPass(in, dryY, dryA);
        float x = highPass(in);
        x = softClip(x * 1.10f) * 0.94f;

        const float detector = lowPass(x, detectY, detectA);
        env += envA * (std::fabs(detector) - env);
        const float thresholdHigh = 0.010f + 0.018f * env;
        const float thresholdLow = -thresholdHigh * 0.72f;

        ++samplesSinceEdge;
        const int minPeriod = (int)(sampleRate / 1150.0f);
        const int maxPeriod = (int)(sampleRate / 42.0f);

        if (armed && detector > thresholdHigh && samplesSinceEdge > minPeriod)
        {
            if (samplesSinceEdge < maxPeriod)
                lastPeriod = samplesSinceEdge;
            samplesSinceEdge = 0;
            armed = false;
            div1 = !div1;
            ++halfCycleCount;
            if ((halfCycleCount & 1) == 0)
                div2 = !div2;
        }
        else if (!armed && detector < thresholdLow)
        {
            armed = true;
        }

        const float targetGate = (env > 0.006f && samplesSinceEdge < lastPeriod * 3) ? 1.0f : 0.0f;
        gate += onePoleCoeffHz(targetGate > gate ? 85.0f : 18.0f, sampleRate) * (targetGate - gate);

        const float square1 = div1 ? 1.0f : -1.0f;
        const float square2 = div2 ? 1.0f : -1.0f;
        float sub1 = lowPass(square1, sub1Y, sub1A);
        float sub2 = lowPass(square2, sub2Y, sub2A);

        // OC-2 style synth voice: darker -2 octave on low Tone, clearer -1
        // octave on high Tone, with envelope following to avoid idle buzz.
        const float t = smoothstep(tone);
        const float oct1Level = 0.72f + 0.42f * t;
        const float oct2Level = 0.54f * (1.0f - 0.58f * t);
        float octave = sub1 * oct1Level + sub2 * oct2Level;
        octave = lowPass(octave, toneY, toneA);
        octave *= gate * (0.52f + 2.85f * std::fmin(env * 7.5f, 1.0f));
        octave = softClip(octave * (1.05f + 0.24f * t));

        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.08f + 1.02f * mix);
        const float dryLevel = 1.0f - 0.72f * m;
        const float wetLevel = (0.34f + 1.34f * m) * m;
        return softClip(dry * dryLevel + octave * wetLevel) * 0.98f;
    }
};

class OctaviusPlugin : public Plugin
{
    OctaviusCore left;
    OctaviusCore right;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    void applyAll()
    {
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    OctaviusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOctaviusDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Octavius"; }
    const char* getDescription() const override { return "OC-2 style octave-down pedal"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'c', 'v', 's'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOctaviusNames[index];
        parameter.symbol = kOctaviusSymbols[index];
        parameter.ranges.min = kOctaviusMin[index];
        parameter.ranges.max = kOctaviusMax[index];
        parameter.ranges.def = kOctaviusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaviusPlugin)
};

Plugin* createPlugin()
{
    return new OctaviusPlugin();
}

END_NAMESPACE_DISTRHO
