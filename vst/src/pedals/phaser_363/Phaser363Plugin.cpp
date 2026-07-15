/*
 * Phaser363 - MXR Phase 90 style one-knob phaser for the game's Pedal_Phaser.
 *
 * Local reference: pedals/phaser 363.png. The schematic is a four-stage JFET
 * all-pass phase shifter with one Rate control. the game exposes the same
 * single knob, so depth, feedback, and mix are fixed internally.
 */
#include "DistrhoPlugin.hpp"
#include "Phaser363Params.h"
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
        const float a = (t - 1.0f) / (t + 1.0f);   // break at fc (the old (1-t)/(1+t) put the corner near Nyquist -> no audible phase shift)
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

} // namespace

class Phaser363Core
{
    float sampleRate = 48000.0f;
    float rate = kPhaser363Def[kRate];
    float phaseOffset = 0.0f;

    FirstOrderAllpass stages[kStageCount];
    OnePoleFilter inputHp;
    OnePoleFilter toneLp;
    OnePoleFilter lfoLag;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    void updateFilters()
    {
        inputHp.setHighPass(sampleRate, 28.0f);
        toneLp.setLowPass(sampleRate, 15000.0f);
        lfoLag.setLowPass(sampleRate, 6.0f + 21.0f * rate);
    }

    float currentRateHz() const
    {
        // Calibrated from the physical-style reference captures at the three
        // panel landmarks: 0.078 Hz, 0.73 Hz and 4.77 Hz.  Spectral modulation
        // shows a strong second harmonic, so counting notch crossings directly
        // would incorrectly report twice the oscillator rate.
        const float shaped = std::pow(clamp01(rate), 0.88f);
        return 0.078f * std::pow(61.2f, shaped);
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
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        toneLp.reset();
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

    float process(float in)
    {
        lfoPhase += currentRateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float phase = lfoPhase + phaseOffset;
        const float sine = std::sin(kTwoPi * phase);
        // The JFET resistance is highly non-linear versus gate voltage; this
        // keeps more of the cycle in the lower part of the sweep instead of
        // making the notch travel linearly in frequency.
        const float lfo = lfoLag.lowPass(std::pow(0.5f + 0.5f * sine, 1.80f));

        // Clean signal path — a Phase 90 is transparent (no input clipping).
        float x = inputHp.highPass(in);
        x = toneLp.lowPass(x);

        // The four IC2-IC5 cells use the same 10k/47nF network and their 2N5952
        // gates share one control voltage.  Small component tolerances stop the
        // notches becoming mathematically infinite without turning the circuit
        // into four unrelated sweeps.
        const float fc = 145.0f * std::pow(18.0f, lfo);
        static const float tolerance[kStageCount] = { 0.990f, 0.997f, 1.003f, 1.010f };
        float shifted = x - feedback;
        for (int i = 0; i < kStageCount; ++i)
            shifted = stages[i].process(shifted, sampleRate, fc * tolerance[i]);
        feedback = shifted * 0.10f;

        // Classic Phase 90 mixer: dry + 4-stage all-pass summed equally -> notches
        // where the all-pass is anti-phase, ~unity broadband. The all-pass is
        // unity-gain, so NO big makeup / output tanh is needed (the old 8.5x*tanh
        // hack distorted to 4.4% THD and pushed peaks to -2.8 dBFS). A transparent
        // soft knee only catches stray peaks.
        float y = (x + shifted) * 0.5f * (1.18f + 0.07f * rate);
        const float ay = std::fabs(y);
        if (ay > 0.80f)
            y = (y < 0.0f ? -1.0f : 1.0f) * (0.80f + 0.16f * std::tanh((ay - 0.80f) / 0.16f));
        return y;
    }
};

class Phaser363Plugin : public Plugin
{
    Phaser363Core left;
    Phaser363Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
    }

public:
    Phaser363Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPhaser363Def[i];
        left.setPhaseOffset(0.00f);
        // The pedal is mono.  Stereo hosts process both input channels, but the
        // two channels must see the same LFO rather than an invented stereo phase.
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Phaser363"; }
    const char* getDescription() const override { return "MXR Phase 90 style one-knob phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', '3', '6', '3'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPhaser363Names[index];
        parameter.symbol = kPhaser363Symbols[index];
        parameter.ranges.min = kPhaser363Min[index];
        parameter.ranges.max = kPhaser363Max[index];
        parameter.ranges.def = kPhaser363Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Phaser363Plugin)
};

Plugin* createPlugin()
{
    return new Phaser363Plugin();
}

END_NAMESPACE_DISTRHO
