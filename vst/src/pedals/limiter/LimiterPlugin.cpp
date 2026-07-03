/*
 * LM-2 - Boss LM-2 Limiter-style pedal.
 *
 * Real panel: Level, Tone, Release, Threshold. Topology is modeled as the
 * service-note circuit: input buffer, uPC1252H2 VCA, M5218/BA718/M5223
 * detector/gain computer, diode rectifier network and transistor output
 * buffer. S-5500G is power protection; 1S188FM-M is approximated with a
 * Sanyo-style germanium detector curve.
 */
#include "DistrhoPlugin.hpp"
#include "LimiterParams.h"
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float dbToAmp(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(std::fmax(amp, 1.0e-8f));
}

static inline float coeffMs(float ms, float sr)
{
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * sr));
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.85f);
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class OnePole
{
    float a = 0.0f;
    float y = 0.0f;

public:
    void setHz(float sr, float hz)
    {
        a = 1.0f - std::exp(-2.0f * kPi * hz / (sr > 1000.0f ? sr : 48000.0f));
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

static inline float transistorBuffer(float x)
{
    return 0.92f * x + 0.08f * std::tanh(1.35f * x) / 1.35f;
}

} // namespace

class LimiterPlugin : public Plugin
{
    float params[kParamCount];
    float sampleRate = 48000.0f;

    RcHighPass inputCapL;
    RcHighPass inputCapR;
    RcHighPass outputCapL;
    RcHighPass outputCapR;
    OnePole toneDarkL;
    OnePole toneDarkR;
    OnePole toneBrightL;
    OnePole toneBrightR;
    rbshared::OpAmpStage inputAmpL;
    rbshared::OpAmpStage inputAmpR;
    rbshared::OpAmpStage detectorAmp;
    rbshared::OpAmpStage outputAmpL;
    rbshared::OpAmpStage outputAmpR;
    rbcomponents::AntiParallelDiodePair detectorClamp;

    float detector = 0.0f;
    float gainReductionDb = 0.0f;
    float thresholdDb = -18.0f;
    float ratio = 12.0f;
    float maxReductionDb = 30.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float grAttackA = 0.0f;
    float grReleaseA = 0.0f;
    float levelGain = 1.0f;
    float tone = kLimiterDef[kTone];

    void recalc()
    {
        const float level = audioTaper(params[kLevel]);
        tone = clamp01(params[kTone]);
        const float release = clamp01(params[kRelease]);
        const float threshold = audioTaper(params[kThreshold]);

        inputCapL.setRC(sampleRate, 1010000.0f, 0.1e-6f);
        inputCapR.setRC(sampleRate, 1010000.0f, 0.1e-6f);
        outputCapL.setRC(sampleRate, 100000.0f, 10.0e-6f);
        outputCapR.setRC(sampleRate, 100000.0f, 10.0e-6f);

        // The LM-2 tone pot crossfades the post-VCA response from low/dark to
        // bright. Keep it gentle; the real pedal is a limiter, not an EQ.
        toneDarkL.setHz(sampleRate, 1450.0f + 2600.0f * tone);
        toneDarkR.setHz(sampleRate, 1450.0f + 2600.0f * tone);
        toneBrightL.setHz(sampleRate, 7200.0f + 6200.0f * tone);
        toneBrightR.setHz(sampleRate, 7200.0f + 6200.0f * tone);

        thresholdDb = -5.0f - 37.0f * threshold;
        ratio = 4.0f + 26.0f * threshold;
        maxReductionDb = 10.0f + 28.0f * threshold;

        attackA = coeffMs(0.55f + 1.2f * (1.0f - threshold), sampleRate);
        releaseA = coeffMs(35.0f + 760.0f * release, sampleRate);
        grAttackA = coeffMs(0.85f + 2.0f * (1.0f - threshold), sampleRate);
        grReleaseA = coeffMs(45.0f + 640.0f * release, sampleRate);
        levelGain = dbToAmp(-28.0f + 42.0f * level);

        detectorClamp.setSpec(rbcomponents::diode1S188FM());
        detectorClamp.setSourceR(47000.0f - 18000.0f * threshold);
    }

    float toneNetwork(float x, OnePole& dark, OnePole& bright) const
    {
        const float low = dark.process(x);
        const float highLimited = bright.process(x);
        const float high = x - 0.60f * highLimited;
        const float t = tone;
        return low * (0.72f * (1.0f - t))
             + high * (0.35f + 0.72f * t)
             + x * 0.30f;
    }

    float gainComputer(float levelDb) const
    {
        const float over = levelDb - thresholdDb;
        const float kneeDb = 4.5f;
        if (over <= -0.5f * kneeDb)
            return 0.0f;

        const float slope = 1.0f - 1.0f / ratio;
        if (over >= 0.5f * kneeDb)
            return std::fmin(maxReductionDb, over * slope);

        const float x = over + 0.5f * kneeDb;
        return std::fmin(maxReductionDb, slope * (x * x) / (2.0f * kneeDb));
    }

public:
    LimiterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kLimiterDef[i];
        sampleRateChanged(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Limiter"; }
    const char* getDescription() const override { return "Boss LM-2-style VCA limiter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('L', 'i', 'm', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kLimiterNames[index];
        parameter.symbol = kLimiterSymbols[index];
        parameter.ranges.min = kLimiterMin[index];
        parameter.ranges.max = kLimiterMax[index];
        parameter.ranges.def = kLimiterDef[index];
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
        recalc();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        sampleRate = (float)newSampleRate;
        if (sampleRate <= 1000.0f)
            sampleRate = 48000.0f;

        inputCapL.reset();
        inputCapR.reset();
        outputCapL.reset();
        outputCapR.reset();
        toneDarkL.reset();
        toneDarkR.reset();
        toneBrightL.reset();
        toneBrightR.reset();

        inputAmpL.setSpec(rbshared::m5218Spec());
        inputAmpR.setSpec(rbshared::m5218Spec());
        detectorAmp.setSpec(rbshared::ba718Spec());
        outputAmpL.setSpec(rbshared::m5223Spec());
        outputAmpR.setSpec(rbshared::m5223Spec());
        inputAmpL.setSampleRate(sampleRate);
        inputAmpR.setSampleRate(sampleRate);
        detectorAmp.setSampleRate(sampleRate);
        outputAmpL.setSampleRate(sampleRate);
        outputAmpR.setSampleRate(sampleRate);

        detectorClamp.reset();
        detector = 0.0f;
        gainReductionDb = 0.0f;
        recalc();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            float l = inputCapL.process(transistorBuffer(inL[i]));
            float r = inputCapR.process(transistorBuffer(inR[i]));
            l = inputAmpL.process(l, 1.8f);
            r = inputAmpR.process(r, 1.8f);

            const float linked = std::fmax(std::fabs(l), std::fabs(r));
            const float rect = std::fabs(detectorClamp.process(linked * 1.85f));
            const float sensed = detectorAmp.process(rect, 4.0f);
            const float envA = sensed > detector ? attackA : releaseA;
            detector += envA * (sensed - detector);
            detector = dn(detector);

            const float targetReduction = gainComputer(ampToDb(detector));
            const float grA = targetReduction > gainReductionDb ? grAttackA : grReleaseA;
            gainReductionDb += grA * (targetReduction - gainReductionDb);
            gainReductionDb = std::fmax(0.0f, std::fmin(maxReductionDb, gainReductionDb));

            // uPC1252H2 gain cell: dB control law, limited so silence cannot
            // explode and heavy limiting stays smooth rather than chattery.
            const float vcaGain = dbToAmp(-gainReductionDb);
            l = outputAmpL.process(l * vcaGain, 2.4f);
            r = outputAmpR.process(r * vcaGain, 2.4f);

            l = toneNetwork(l, toneDarkL, toneBrightL);
            r = toneNetwork(r, toneDarkR, toneBrightR);
            l = outputCapL.process(l * levelGain);
            r = outputCapR.process(r * levelGain);

            outL[i] = std::tanh(l * 1.05f) * 0.98f;
            outR[i] = std::tanh(r * 1.05f) * 0.98f;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterPlugin)
};

Plugin* createPlugin()
{
    return new LimiterPlugin();
}

END_NAMESPACE_DISTRHO
