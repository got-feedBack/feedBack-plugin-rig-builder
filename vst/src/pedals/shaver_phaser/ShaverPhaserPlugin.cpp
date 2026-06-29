/*
 * ShaverPhaser - Boss PH-1R style phaser for Pedal_ShaverPhaser.
 *
 * Local reference: pedals/boss_ph-1r_phaser_pedal.png. The circuit is a
 * four-stage uPC4558 all-pass phaser whose 2SK30A JFETs are swept by a TL022
 * LFO/control network. Real controls are Rate, Depth, and Resonance.
 */
#include "DistrhoPlugin.hpp"
#include "ShaverPhaserParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kStageCount = 4;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
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

class ShaverPhaserCore
{
    float sampleRate = 48000.0f;
    float phaseOffset = 0.0f;
    float rate = kShaverPhaserDef[kRate];
    float depth = kShaverPhaserDef[kDepth];
    float resonance = kShaverPhaserDef[kResonance];

    FirstOrderAllpass stages[kStageCount];
    HighPass inputHp;
    OnePole inputTone;
    OnePole lfoLag;
    OnePole jfetLag;
    OnePole outputTone;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;
    float jfetCv = 0.5f;

    void updateFilters()
    {
        const float d = smoothstep(depth);
        const float r = smoothstep(resonance);
        inputHp.set(sampleRate, 28.0f);
        inputTone.setLowPass(sampleRate, 7600.0f - 900.0f * d - 450.0f * r);
        outputTone.setLowPass(sampleRate, 8200.0f - 1050.0f * r);
        lfoLag.setLowPass(sampleRate, 4.0f + 19.0f * rate);
        jfetLag.setLowPass(sampleRate, 30.0f);
    }

    float currentRateHz() const
    {
        return 0.055f + 7.10f * audioTaper(rate);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = 0.0f;
        jfetCv = 0.5f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        inputTone.reset();
        lfoLag.reset();
        jfetLag.reset();
        outputTone.reset();
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

    void setResonance(float v)
    {
        resonance = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.10f + 0.90f * smoothstep(depth);
        const float r = smoothstep(resonance);
        const float phase = lfoPhase + phaseOffset;
        const float tri = 1.0f - 4.0f * std::fabs((phase - std::floor(phase)) - 0.5f);
        const float sine = std::sin(kTwoPi * phase);
        const float rawLfo = 0.5f + 0.5f * (0.63f * sine + 0.37f * tri);
        const float lfo = lfoLag.process(clamp01(rawLfo));

        // PH-1R selected 2SK30A-GR/Y FETs act as voltage-controlled resistors.
        // This maps the TL022 LFO voltage into a broad but bounded VCR range.
        const float centre = 0.50f + (lfo - 0.5f) * d;
        jfetCv = jfetLag.process(clamp01(centre));
        const float fetShape = std::pow(jfetCv, 1.22f);

        // Clean path — a PH-1R phaser is transparent (no input clipping).
        float x = inputHp.process(in);
        x = inputTone.process(x);

        static const float baseHz[kStageCount] = { 105.0f, 245.0f, 565.0f, 1320.0f };
        float shifted = x - feedback * (0.20f + 0.55f * r);   // Resonance -> notch depth
        for (int i = 0; i < kStageCount; ++i)
        {
            float cv = fetShape + 0.075f * (float)i;
            if (cv > 1.0f)
                cv -= 1.0f;
            const float sweep = 0.34f + (8.7f + 4.4f * d) * smoothstep(cv);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep);
        }

        feedback = std::tanh(shifted);                 // regen only (bounded, not in the wet path)
        const float wet = outputTone.process(shifted); // clean all-pass output

        // Clean dry + all-pass mix (~unity). The old x*0.82 - wet*amt then
        // tanh(y*5.10) ran a 5.1x makeup into a tanh -> 2.2% THD on hot signals
        // and squashed peaks. A transparent soft knee only catches stray peaks.
        float y = (x + wet) * 0.5f * 1.05f;
        const float ay = std::fabs(y);
        if (ay > 0.80f)
            y = (y < 0.0f ? -1.0f : 1.0f) * (0.80f + 0.16f * std::tanh((ay - 0.80f) / 0.16f));
        return y;
    }
};

class ShaverPhaserPlugin : public Plugin
{
    ShaverPhaserCore left;
    ShaverPhaserCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
        left.setResonance(params[kResonance]);
        right.setResonance(params[kResonance]);
    }

public:
    ShaverPhaserPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kShaverPhaserDef[i];
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.018f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ShaverPhaser"; }
    const char* getDescription() const override { return "Boss PH-1R style JFET phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'h', 'P', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kShaverPhaserNames[index];
        parameter.symbol = kShaverPhaserSymbols[index];
        parameter.ranges.min = kShaverPhaserMin[index];
        parameter.ranges.max = kShaverPhaserMax[index];
        parameter.ranges.def = kShaverPhaserDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShaverPhaserPlugin)
};

Plugin* createPlugin()
{
    return new ShaverPhaserPlugin();
}

END_NAMESPACE_DISTRHO
