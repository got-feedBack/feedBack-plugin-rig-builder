/*
 * BobFilter - Moog MF-105M/MuRF-style analog filter bank.
 * The local schematic shows TL072/LF412/LM837 op-amp conditioning, LM13700
 * direct VCA, eight FILT/VCA cells gated by CD4016/DG445 analog switches,
 * CD4051/4053 control muxing, BC850/BC860 control buffers and bass/mids
 * voicing. The plugin exposes a practical real-panel set: Drive, Output,
 * Pattern, Rate, Envelope, Mix and Mode.
 */
#include "DistrhoPlugin.hpp"
#include "BobFilterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr int kBandCount = 8;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float timeCoeff(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return std::exp(-1.0f / samples);
}

static inline float onePoleCoeff(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setBandPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(alpha, 0.0f, -alpha, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
};

class Svf
{
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

public:
    void reset()
    {
        ic1eq = ic2eq = 0.0f;
    }

    void process(float x, float sampleRate, float hz, float q,
                 float& low, float& band, float& high)
    {
        hz = clampFreq(hz, sampleRate);
        q = std::fmax(0.48f, std::fmin(q, 16.0f));

        const float g = std::tan(kPi * hz / sampleRate);
        const float r = 1.0f / (2.0f * q);
        const float h = 1.0f / (1.0f + 2.0f * r * g + g * g);
        const float v3 = x - ic2eq;
        const float v1 = h * (g * v3 + ic1eq);
        const float v2 = ic2eq + g * v1;

        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        low = v2;
        band = v1;
        high = x - 2.0f * r * v1 - v2;
    }
};

class EnvelopeFollower
{
    float env = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

public:
    void reset()
    {
        env = 0.0f;
    }

    void setTimes(float sr, float attackMs, float releaseMs)
    {
        attackCoeff = timeCoeff(attackMs, sr);
        releaseCoeff = timeCoeff(releaseMs, sr);
    }

    float process(float x)
    {
        const float target = std::fabs(x);
        const float coeff = target > env ? attackCoeff : releaseCoeff;
        env = target + coeff * (env - target);
        return env;
    }
};

} // namespace

// Moog MF-105/MF-105M MuRF — circuit-real model (Moog schematic, Rev A 2009).
// A STATIC bank of 8 multiple-feedback bandpass filters at fixed centers (two
// voicings: MIDS and BASS, switched by MODE). The animation is NOT a swept wah
// (the old model invented one) — it's a stepped SEQUENCER that gates each band's
// VCA: a pattern picks which bands fire on each step, RATE clocks the steps, and
// each band's level is an attack/decay envelope whose DECAY length is the ENVELOPE
// pot (short = staccato gating, long = smooth bloom). DRIVE = gentle input overload,
// MIX = dry/wet, OUTPUT = clean level.
//
// Pattern set: the real per-step tables live in the unit's MCU firmware (not on the
// schematic), so these are a faithful RECONSTRUCTION of documented MuRF behaviour.
// Each byte = a bitmask of the 8 bands ON for that step (bit i = band i, 0=lowest).
static const int kPatternLen[12] = { 1, 8, 8, 2, 14, 4, 2, 2, 6, 4, 4, 4 };
static const unsigned char kPatterns[12][16] = {
    { 0xFF },                                                                   // 0 Drone (all on = static resonator)
    { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 },                                // 1 Up walk (low->high)
    { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01 },                                // 2 Down walk
    { 0x55,0xAA },                                                              // 3 Odd/even alternate
    { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x40,0x20,0x10,0x08,0x04,0x02 },  // 4 Up-down bounce
    { 0x03,0x0C,0x30,0xC0 },                                                    // 5 Two-band cluster sweep
    { 0x07,0x00 },                                                             // 6 Low pulse (bands 0-2)
    { 0xE0,0x00 },                                                             // 7 High pulse (bands 5-7)
    { 0x49,0x92,0x24,0xB6,0x6D,0xDB },                                          // 8 Pseudo-random
    { 0x15,0x2A,0x54,0xA8 },                                                    // 9 Every-other walking
    { 0x81,0x42,0x24,0x18 },                                                    // 10 Inward (edges -> center)
    { 0x18,0x24,0x42,0x81 },                                                    // 11 Outward (center -> edges)
};

class BobFilterCore
{
    float sampleRate = 48000.0f;
    float drive = kBobFilterDef[kDrive];
    float output = kBobFilterDef[kOutput];
    float pattern = kBobFilterDef[kPattern];
    float rate = kBobFilterDef[kRate];
    float envelope = kBobFilterDef[kEnvelope];
    float mix = kBobFilterDef[kMix];
    float mode = kBobFilterDef[kMode];

    Biquad bands[kBandCount];
    float gate[kBandCount];      // per-band VCA gain (the animation envelope)
    int gPhase[kBandCount];      // 0 = attack, 1 = decay
    float stepPhase = 0.0f;      // [0,1) within the current step
    int stepIndex = 0;
    float hpX1 = 0.0f, hpY1 = 0.0f, hpA = 0.0f;

    float atkA = 0.0f, decA = 0.0f;

    int patternIndex() const
    {
        int p = (int)(clamp01(pattern) * 11.999f);
        return p < 0 ? 0 : (p > 11 ? 11 : p);
    }

    void retrigger(int step)
    {
        const int p = patternIndex();
        const unsigned char mask = kPatterns[p][step % kPatternLen[p]];
        for (int i = 0; i < kBandCount; ++i)
            if (mask & (1 << i))
                gPhase[i] = 0;   // attack (fire this band)
    }

    void updateFilters()
    {
        // Band centers straight from the schematic (Hz). MODE = voicing switch.
        static const float midsCenters[kBandCount] = {
            200.0f, 300.0f, 450.0f, 675.0f, 1000.0f, 1500.0f, 2200.0f, 3400.0f,
        };
        static const float bassCenters[kBandCount] = {
            110.0f, 160.0f, 240.0f, 350.0f, 525.0f, 775.0f, 1200.0f, 1800.0f,
        };
        const float* centers = mode >= 0.5f ? bassCenters : midsCenters;
        // MFB bandpass Q is fixed and mild (~2.5) — the MuRF bands deliberately
        // overlap; Q must NOT depend on Drive (the old model's big mistake).
        for (int i = 0; i < kBandCount; ++i)
            bands[i].setBandPass(sampleRate, centers[i], 2.5f);

        // per-band animation envelope: fast attack, ENVELOPE pot = decay length.
        atkA = 1.0f - std::exp(-1.0f / (0.003f * sampleRate));             // 3 ms attack
        const float decMs = 20.0f + 1500.0f * audioTaper(envelope);       // staccato -> bloom
        decA = 1.0f - std::exp(-1.0f / (0.001f * decMs * sampleRate));

        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * 30.0f);                     // input HP ~30 Hz
        hpA = rc / (rc + dt);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

public:
    void reset()
    {
        stepPhase = hpX1 = hpY1 = 0.0f;
        stepIndex = 0;
        for (int i = 0; i < kBandCount; ++i)
        {
            bands[i].reset();
            gate[i] = 0.0f;
            gPhase[i] = 1;   // start decayed
        }
        updateFilters();
        retrigger(0);        // fire the first step
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setDrive(float v)    { drive = clamp01(v); }
    void setOutput(float v)   { output = clamp01(v); }
    void setPattern(float v)  { pattern = clamp01(v); }
    void setRate(float v)     { rate = clamp01(v); }
    void setEnvelope(float v) { envelope = clamp01(v); updateFilters(); }
    void setMix(float v)      { mix = clamp01(v); }
    void setMode(float v)     { mode = clamp01(v); updateFilters(); }

    float process(float in)
    {
        float x = highPass(in);

        // DRIVE — gentle pre-filter input overload (NOT a fuzz; unity at small signal)
        const float dg = 1.0f + 2.2f * audioTaper(drive);
        x = std::tanh(x * dg) * (1.0f / std::tanh(dg)) * 0.85f;

        // --- sequencer clock: advance the step, retrigger the firing bands ---
        const float stepHz = 0.3f * std::pow(30.0f, clamp01(rate));   // ~0.3 .. 9 Hz
        stepPhase += stepHz / sampleRate;
        if (stepPhase >= 1.0f)
        {
            stepPhase -= 1.0f;
            stepIndex = (stepIndex + 1) % kPatternLen[patternIndex()];
            retrigger(stepIndex);
        }

        // --- per-band VCA envelopes (attack to 1, then decay over the ENV time) ---
        float bank = 0.0f;
        for (int i = 0; i < kBandCount; ++i)
        {
            if (gPhase[i] == 0)
            {
                gate[i] += atkA * (1.0f - gate[i]);
                if (gate[i] > 0.99f) gPhase[i] = 1;
            }
            else
            {
                gate[i] += decA * (0.0f - gate[i]);
            }
            bank += bands[i].process(x) * gate[i];
        }
        bank *= 0.42f;   // sum of 8 resonant bands -> tame to ~unity

        // MIX dry/wet, OUTPUT clean level
        const float dry = x;
        float y = dry * (1.0f - mix) + bank * mix * 1.9f;
        y *= 0.15f + 1.70f * audioTaper(output);
        return y;
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
        left.setPattern(params[kPattern]);
        right.setPattern(params[kPattern]);
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
        left.setEnvelope(params[kEnvelope]);
        right.setEnvelope(params[kEnvelope]);
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
    const char* getDescription() const override { return "Moog-style analog filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
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
