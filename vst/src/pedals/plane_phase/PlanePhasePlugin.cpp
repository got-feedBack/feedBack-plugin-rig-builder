/*
 * PlanePhase - AP-7 style eight-stage phaser for Rocksmith's Pedal_PlanePhase.
 *
 * Local reference: pedals/plane phase.gif. The schematic shows a driven input,
 * 8-stage phase converter, Resonance, Jet Level, and Rate controls. Rocksmith
 * exposes Rate, Depth, and Mix, so resonance and jet emphasis are internal.
 */
#include "DistrhoPlugin.hpp"
#include "PlanePhaseParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kStageCount = 8;

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
    if (hz < 18.0f)
        return 18.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

class OnePoleFilter
{
    float lp = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float lpA = 0.0f;
    float hpA = 0.0f;

public:
    void reset()
    {
        lp = hpX1 = hpY1 = 0.0f;
    }

    void setLowPass(float sr, float hz)
    {
        lpA = onePoleCoeffHz(hz, sr);
    }

    void setHighPass(float sr, float hz)
    {
        const float dt = 1.0f / sr;
        const float rc = 1.0f / (2.0f * kPi * clampFreq(hz, sr));
        hpA = rc / (rc + dt);
    }

    float lowPass(float x)
    {
        lp += lpA * (x - lp);
        return lp;
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }
};

class FirstOrderAllpass
{
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    float process(float x, float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float t = std::tan(kPi * hz / sr);
        const float a = (1.0f - t) / (1.0f + t);
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class PlanePhaseCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float rate = kPlanePhaseDef[kRate];
    float depth = kPlanePhaseDef[kDepth];
    float mix = kPlanePhaseDef[kMix];

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter driveTone;
    OnePoleFilter outputLp;
    OnePoleFilter lfoLag;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;
    float env = 0.0f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        const float m = smoothstep(mix);
        inputHp.setHighPass(sampleRate, 32.0f);
        driveTone.setLowPass(sampleRate, 7200.0f - 1100.0f * d);
        outputLp.setLowPass(sampleRate, 6200.0f - 1350.0f * d - 550.0f * m);
        lfoLag.setLowPass(sampleRate, 7.5f + 20.0f * rate);
    }

    float currentRateHz() const
    {
        return 0.050f + 5.15f * std::pow(clamp01(rate), 1.44f);
    }

    float lfoValue()
    {
        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(kTwoPi * phase);
        const float asym = 0.5f + 0.5f * (0.82f * sine + 0.18f * std::sin(kTwoPi * (phase * 2.0f + 0.11f)));
        return lfoLag.lowPass(std::pow(clamp01(asym), 1.18f));
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = env = 0.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        driveTone.reset();
        outputLp.reset();
        lfoLag.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
        updateFilters();
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.03f + 0.97f * smoothstep(depth);
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.10f + 1.05f * mix);
        const float lfo = lfoValue();

        float x = inputHp.highPass(in);
        x = driveTone.lowPass(x);
        env += onePoleCoeffHz(18.0f, sampleRate) * (std::fabs(x) - env);
        const float driven = std::tanh(x * (1.10f + 0.42f * d + 0.18f * env)) * 0.88f;

        static const float baseHz[kStageCount] = { 68.0f, 106.0f, 178.0f, 305.0f, 520.0f, 890.0f, 1510.0f, 2460.0f };
        const float resonance = (0.30f + 0.38f * d + 0.18f * m);
        float shifted = driven - feedback * resonance;
        for (int i = 0; i < kStageCount; ++i)
        {
            float stageLfo = lfo + 0.055f * (float)i;
            if (stageLfo > 1.0f)
                stageLfo -= 1.0f;
            const float sweep = 0.18f + (13.6f + 5.2f * d) * smoothstep(stageLfo);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }

        feedback = std::tanh(shifted) * (0.46f + 0.20f * d);
        const float jet = shifted - driven * (0.18f + 0.12f * d);
        const float wet = outputLp.lowPass(std::tanh(jet * (1.08f + 0.24f * m)));

        const float dryLevel = 1.0f - 0.30f * m;
        const float wetLevel = (0.34f + 1.05f * m) * m;
        const float y = driven * dryLevel - wet * wetLevel;
        return std::tanh(y * 0.93f) * 0.98f;
    }
};

class PlanePhasePlugin : public Plugin
{
    PlanePhaseCore left;
    PlanePhaseCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    PlanePhasePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPlanePhaseDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.025f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "PlanePhase"; }
    const char* getDescription() const override { return "AP-7 style eight-stage phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', 'P', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPlanePhaseNames[index];
        parameter.symbol = kPlanePhaseSymbols[index];
        parameter.ranges.min = kPlanePhaseMin[index];
        parameter.ranges.max = kPlanePhaseMax[index];
        parameter.ranges.def = kPlanePhaseDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlanePhasePlugin)
};

Plugin* createPlugin()
{
    return new PlanePhasePlugin();
}

END_NAMESPACE_DISTRHO
