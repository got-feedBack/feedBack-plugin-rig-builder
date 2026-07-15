/*
 * BassPhase - Ibanez PH99 style opto phaser for Bass_Pedal_BassPhase.
 *
 * Local reference: pedals/ibanez_ph99.pdf. The schematic uses a NE571
 * compander around a six-stage 4558/C4570 all-pass network, TL022 LFO, two
 * P873-G35 optocouplers, and real Speed, Depth, Feedback, Level controls.
 */
#include "DistrhoPlugin.hpp"
#include "BassPhaseParams.h"
#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kStageCount = 6;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 16.0f)
        return 16.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

class OnePole
{
    float z = 0.0f;
    float a = 0.0f;

public:
    void reset() { z = 0.0f; }
    void setLowPass(float sr, float hz) { a = onePoleCoeffHz(hz, sr); }
    float process(float x)
    {
        z += a * (x - z);
        return z;
    }
};

class HighPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float a = 0.0f;

public:
    void reset() { x1 = y1 = 0.0f; }
    void set(float sr, float hz)
    {
        const float dt = 1.0f / sr;
        const float rc = 1.0f / (2.0f * kPi * clampFreq(hz, sr));
        a = rc / (rc + dt);
    }
    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class FirstOrderAllpass
{
    float z = 0.0f;

public:
    void reset() { z = 0.0f; }
    float process(float x, float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float t = std::tan(kPi * hz / sr);
        const float a = (t - 1.0f) / (t + 1.0f);   // FIX: break at fc (was near-Nyquist -> no audible phaser)
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class BassPhaseCore
{
    float sampleRate = 48000.0f;
    float speed = kBassPhaseDef[kSpeed];
    float depth = kBassPhaseDef[kDepth];
    float feedbackKnob = kBassPhaseDef[kFeedback];
    float level = kBassPhaseDef[kLevel];
    float phaseOffset = 0.0f;

    FirstOrderAllpass stages[kStageCount];
    HighPass inputHp;
    HighPass wetLowGuard;
    OnePole inputTone;
    OnePole outputTone;
    OnePole lfoLagA;
    OnePole lfoLagB;
    OnePole compEnv;

    float lfoPhase = 0.0f;
    float feedbackState = 0.0f;
    float companderGain = 1.0f;

    void updateFilters()
    {
        inputHp.set(sampleRate, 24.0f);
        wetLowGuard.set(sampleRate, 74.0f);
        inputTone.setLowPass(sampleRate, 15000.0f);
        outputTone.setLowPass(sampleRate, 13500.0f);
        lfoLagA.setLowPass(sampleRate, 10.0f);
        lfoLagB.setLowPass(sampleRate, 8.5f);
        compEnv.setLowPass(sampleRate, 18.0f);
    }

    float currentRateHz() const
    {
        const float shaped = std::pow(clamp01(speed), 0.92f);
        return 0.055f * std::pow(112.0f, shaped);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedbackState = 0.0f;
        companderGain = 1.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        wetLowGuard.reset();
        inputTone.reset();
        outputTone.reset();
        lfoLagA.reset();
        lfoLagB.reset();
        compEnv.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setSpeed(float v)
    {
        speed = clamp01(v);
        updateFilters();
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    void setFeedback(float v)
    {
        feedbackKnob = clamp01(v);
        updateFilters();
    }

    void setLevel(float v)
    {
        level = clamp01(v);
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.08f + 0.92f * smoothstep(depth);
        const float fb = smoothstep(feedbackKnob);
        const float p = lfoPhase + phaseOffset;

        // TL022 oscillator in the PH99 is closer to rounded triangle than a
        // pure sine. The two P873-G35 optos do not track identically, so keep
        // two lagged control voltages with slight offset.
        const float wrapped = p - std::floor(p);
        const float tri = 1.0f - 4.0f * std::fabs(wrapped - 0.5f);
        const float sine = std::sin(kTwoPi * wrapped);
        const float osc = clamp01(0.5f + 0.5f * (0.58f * tri + 0.42f * sine));
        const float optoA = lfoLagA.process(clamp01(0.50f + (osc - 0.5f) * d));
        const float optoB = lfoLagB.process(clamp01(0.515f + (osc - 0.5f) * (0.96f * d)));

        float x = inputHp.process(in);
        x = inputTone.process(x);

        // The NE571 compressor and expander surround the all-pass path.  Their
        // gain must cancel at the output; treating the compressor as a drive
        // stage caused the previous 6-8 dB boost and audible pumping.
        const float env = compEnv.process(std::fabs(x));
        const float targetGain = 1.0f / std::sqrt(0.08f + 4.0f * env);
        companderGain += onePoleCoeffHz(14.0f, sampleRate) * (targetGain - companderGain);
        const float compressorScale = 0.55f * companderGain;
        const float compressed = x * compressorScale;

        // IC3A through IC3B are six equal 10k/3.3nF cells.  PH1 and PH2 see
        // the same lamp drive, with only optocoupler/component mismatch.
        static const float tolerance[kStageCount] = { 0.970f, 0.985f, 0.995f, 1.005f, 1.018f, 1.035f };
        const float regen = 0.05f + 0.50f * fb;
        float shifted = compressed - feedbackState * regen;

        for (int i = 0; i < kStageCount; ++i)
        {
            const float cv = (i & 1) ? optoB : optoA;
            const float corner = 95.0f * std::pow(38.0f, std::pow(clamp01(cv), 1.35f));
            shifted = stages[i].process(shifted, sampleRate, corner * tolerance[i]);
        }

        feedbackState = shifted;
        const float wet = outputTone.process(shifted / std::max(0.20f, compressorScale));

        const float outLevel = level / kBassPhaseDef[kLevel];
        float y = (x + wet) * 0.5f * 1.12f * outLevel;
        const float ay = std::fabs(y);
        if (ay > 0.88f)
            y = (y < 0.0f ? -1.0f : 1.0f) * (0.88f + 0.10f * std::tanh((ay - 0.88f) / 0.10f));
        return y;
    }
};

class BassPhasePlugin : public Plugin
{
    BassPhaseCore left;
    BassPhaseCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setFeedback(params[kFeedback]);
        right.setFeedback(params[kFeedback]);
        left.setLevel(params[kLevel]);
        right.setLevel(params[kLevel]);
    }

public:
    BassPhasePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassPhaseDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        const float sr = (float)getSampleRate();
        left.setSampleRate(sr);
        right.setSampleRate(sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassPhase"; }
    const char* getDescription() const override { return "Ibanez PH99 style opto phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'P', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassPhaseNames[index];
        parameter.symbol = kBassPhaseSymbols[index];
        parameter.ranges.min = kBassPhaseMin[index];
        parameter.ranges.max = kBassPhaseMax[index];
        parameter.ranges.def = kBassPhaseDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassPhasePlugin)
};

Plugin* createPlugin()
{
    return new BassPhasePlugin();
}

END_NAMESPACE_DISTRHO
