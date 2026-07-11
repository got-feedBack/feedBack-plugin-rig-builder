/*
 * BobFilter — Moog MF-101 Lowpass Filter (envelope filter), rebuilt.
 *
 * The game's "Bob Filter" gear has an envelope-filter panel (Sens / Attack /
 * Release / Mix / Filter), i.e. the Moog envelope filter: an envelope follower
 * sweeping a 4-pole transistor-ladder lowpass with resonance. The previous
 * model here was the MF-105 MuRF (a pattern-SEQUENCED static filter bank) —
 * a different pedal that steps a rhythm instead of tracking the pick, which
 * is why it never sounded like an envelope filter in game tones.
 *
 * Architecture (MF-101 signal path):
 *   Drive -> envelope follower (Attack/Release, Amount in octaves)
 *         -> 4-stage tanh transistor ladder (Huovilainen-style, 2x oversampled)
 *            with resonance feedback up to the edge of self-oscillation
 *         -> 2-pole / 4-pole output tap (the real panel switch)
 *         -> Mix -> Output.
 */
#include "DistrhoPlugin.hpp"
#include "BobFilterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return 1.0f - std::exp(-1.0f / samples);
}

// 4-stage transistor-ladder LPF (Huovilainen-style tanh saturated integrators),
// run 2x oversampled by the caller for stability at high cutoff + resonance.
class LadderFilter
{
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;

public:
    void reset() { s1 = s2 = s3 = s4 = 0.0f; }

    // g = 1-exp(-2π fc/sr) (per oversampled step), k = resonance feedback 0..~4.2
    // Returns via out2 (2-pole tap) and out4 (4-pole tap).
    inline void process(float x, float g, float k, float& out2, float& out4)
    {
        const float in = x - k * s4;
        s1 += g * (std::tanh(in) - std::tanh(s1));
        s2 += g * (std::tanh(s1) - std::tanh(s2));
        s3 += g * (std::tanh(s2) - std::tanh(s3));
        s4 += g * (std::tanh(s3) - std::tanh(s4));
        s1 = dn(s1); s2 = dn(s2); s3 = dn(s3); s4 = dn(s4);
        out2 = s2;
        out4 = s4;
    }
};

} // namespace

class BobFilterCore
{
    float sampleRate = 48000.0f;
    float drive = kBobFilterDef[kDrive];
    float output = kBobFilterDef[kOutput];
    float cutoff = kBobFilterDef[kCutoff];
    float resonance = kBobFilterDef[kResonance];
    float envAmount = kBobFilterDef[kEnvelope];
    float attack = kBobFilterDef[kAttack];
    float release = kBobFilterDef[kRelease];
    float mix = kBobFilterDef[kMix];
    float mode = kBobFilterDef[kMode];

    LadderFilter ladder;
    float env = 0.0f;
    float atkA = 0.0f, relA = 0.0f;
    float dcIn = 0.0f;
    float fcSmooth = 200.0f;

    void updateCoeffs()
    {
        // Game Attack observed 1-250 ms; Release wider. Audio-taper the pots.
        const float atkMs = 2.0f + 298.0f * audioTaper(attack);
        const float relMs = 25.0f + 900.0f * audioTaper(release);
        atkA = onePoleCoeffMs(atkMs, sampleRate);
        relA = onePoleCoeffMs(relMs, sampleRate);
    }

public:
    void reset()
    {
        ladder.reset();
        env = dcIn = 0.0f;
        fcSmooth = 200.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setDrive(float v)     { drive = clamp01(v); }
    void setOutput(float v)    { output = clamp01(v); }
    void setCutoff(float v)    { cutoff = clamp01(v); }
    void setResonance(float v) { resonance = clamp01(v); }
    void setEnvelope(float v)  { envAmount = clamp01(v); }
    void setAttack(float v)    { attack = clamp01(v); updateCoeffs(); }
    void setRelease(float v)   { release = clamp01(v); updateCoeffs(); }
    void setMix(float v)       { mix = clamp01(v); }
    void setMode(float v)      { mode = clamp01(v); }

    float process(float in)
    {
        // input DC blocker (~7 Hz)
        dcIn += 0.0009f * (in - dcIn);
        float x = in - dcIn;

        // DRIVE — MF-101 input gain into the ladder (gentle overload at max)
        const float dg = 1.0f + 5.5f * audioTaper(drive);
        x *= dg;

        // ── envelope follower: |x| with Attack/Release, soft-knee normalize.
        //    Fixed detector gain calibrated so guitar-level input (peaks around
        //    -12 dBFS) drives the knee well into its range — an envelope filter
        //    that doesn't move is just a dark EQ. ──
        const float rect = std::fabs(x) * 5.0f;
        env += (rect > env ? atkA : relA) * (rect - env);
        const float e01 = env / (env + 0.40f);        // 0..~1, playing-level knee

        // ── cutoff: base (Cutoff pot, log 60 Hz-6 kHz) + envelope sweep in
        //    octaves (Amount pot, up to +4.5 oct — the MF-101 upward sweep) ──
        const float base = 60.0f * std::pow(100.0f, clamp01(cutoff));   // 60..6000
        const float oct = 4.5f * clamp01(envAmount) * e01;
        float fc = base * std::pow(2.0f, oct);
        const float fcMax = sampleRate * 0.40f;
        if (fc > fcMax) fc = fcMax;
        if (fc < 40.0f) fc = 40.0f;
        fcSmooth += 0.004f * (fc - fcSmooth);          // de-zipper the sweep

        // ── resonance: up to the edge of self-oscillation ──
        const float k = 4.1f * audioTaper(resonance);

        // ── 4-stage tanh ladder, 2x oversampled ──
        const float g = 1.0f - std::exp(-2.0f * kPi * fcSmooth / (2.0f * sampleRate));
        float o2 = 0.0f, o4 = 0.0f;
        ladder.process(x, g, k, o2, o4);
        ladder.process(x, g, k, o2, o4);

        float wet = (mode >= 0.5f) ? o4 : o2;
        // passband-loss compensation as resonance rises (ladder classic)
        wet *= 1.0f + 0.55f * k;

        float y = x * (1.0f - mix) + wet * mix;
        // +2.4x wet makeup: a mostly-closed 4-pole ladder eats broadband level
        y *= (0.20f + 1.60f * audioTaper(output)) * 2.4f / std::sqrt(dg);
        return std::tanh(y * 1.05f) * 0.95f;
    }
};

class BobFilterPlugin : public Plugin
{
    BobFilterCore left;
    BobFilterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setDrive(params[kDrive]);
        right.setDrive(params[kDrive]);
        left.setOutput(params[kOutput]);
        right.setOutput(params[kOutput]);
        left.setCutoff(params[kCutoff]);
        right.setCutoff(params[kCutoff]);
        left.setResonance(params[kResonance]);
        right.setResonance(params[kResonance]);
        left.setEnvelope(params[kEnvelope]);
        right.setEnvelope(params[kEnvelope]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
        left.setRelease(params[kRelease]);
        right.setRelease(params[kRelease]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
    }

public:
    BobFilterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBobFilterDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BobFilter"; }
    const char* getDescription() const override { return "Moog MF-101 style envelope filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'f', 'l', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBobFilterNames[index];
        parameter.symbol = kBobFilterSymbols[index];
        parameter.ranges.min = kBobFilterMin[index];
        parameter.ranges.max = kBobFilterMax[index];
        parameter.ranges.def = kBobFilterDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BobFilterPlugin)
};

Plugin* createPlugin()
{
    return new BobFilterPlugin();
}

END_NAMESPACE_DISTRHO
