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
    Svf wah;
    EnvelopeFollower env;

    float lp = 0.0f;
    float wahCutoff = 420.0f;
    float stepPhase = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float hpA = 0.0f;

    void updateFilters()
    {
        static const float bassCenters[kBandCount] = {
            110.0f, 160.0f, 240.0f, 350.0f,
            525.0f, 775.0f, 1200.0f, 1800.0f,
        };
        static const float midsCenters[kBandCount] = {
            200.0f, 300.0f, 450.0f, 675.0f,
            1000.0f, 1500.0f, 2200.0f, 3400.0f,
        };
        const float* centers = mode >= 0.5f ? bassCenters : midsCenters;
        const float q = 5.2f + 4.4f * audioTaper(drive);
        for (int i = 0; i < kBandCount; ++i)
            bands[i].setBandPass(sampleRate, centers[i], q);

        const float e = audioTaper(envelope);
        const float attackMs = 2.0f + 34.0f * (1.0f - e);
        const float releaseMs = 44.0f + 235.0f * (1.0f - e);
        env.setTimes(sampleRate, attackMs, releaseMs);

        const float hpHz = 30.0f;
        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * hpHz);
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
        lp = stepPhase = hpX1 = hpY1 = 0.0f;
        wahCutoff = 420.0f;
        env.reset();
        wah.reset();
        for (int i = 0; i < kBandCount; ++i)
            bands[i].reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setDrive(float v)
    {
        drive = clamp01(v);
        updateFilters();
    }

    void setOutput(float v)
    {
        output = clamp01(v);
    }

    void setPattern(float v)
    {
        pattern = clamp01(v);
    }

    void setRate(float v)
    {
        rate = clamp01(v);
    }

    void setEnvelope(float v)
    {
        envelope = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setMode(float v)
    {
        mode = clamp01(v);
        updateFilters();
    }

    float advancePattern()
    {
        const float rateHz = 0.08f + 8.4f * std::pow(clamp01(rate), 1.85f);
        stepPhase += rateHz / sampleRate;
        stepPhase -= std::floor(stepPhase);
        return stepPhase * (float)kBandCount;
    }

    float patternGate(int band, float activeBand) const
    {
        static const float patterns[6][8] = {
            { 1.00f, 0.35f, 0.85f, 0.25f, 0.75f, 0.40f, 0.90f, 0.30f },
            { 0.20f, 0.95f, 0.30f, 0.85f, 0.45f, 0.75f, 0.55f, 1.00f },
            { 1.00f, 0.15f, 0.15f, 0.90f, 0.25f, 0.25f, 0.82f, 0.35f },
            { 0.35f, 0.60f, 1.00f, 0.50f, 0.28f, 0.88f, 0.42f, 0.72f },
            { 0.85f, 0.75f, 0.62f, 0.48f, 0.35f, 0.52f, 0.70f, 1.00f },
            { 0.55f, 1.00f, 0.72f, 0.35f, 0.92f, 0.46f, 0.80f, 0.28f },
        };
        const int p = (int)std::floor(clamp01(pattern) * 5.99f);
        const float seq = patterns[p < 0 ? 0 : (p > 5 ? 5 : p)][band];
        const float d = std::fabs(activeBand - (float)band);
        const float wrapped = std::fmin(d, (float)kBandCount - d);
        const float pulse = std::fmax(0.0f, 1.0f - wrapped * 0.85f);
        return 0.25f + 0.75f * (seq * (0.30f + 0.70f * pulse));
    }

    float process(float in)
    {
        float x = highPass(in);
        const float driveGain = 0.95f + 5.2f * audioTaper(drive);
        x = std::tanh(x * driveGain);
        const float bassMode = mode >= 0.5f ? 1.0f : 0.0f;

        const float dTaper = audioTaper(drive);
        const float eTaper = audioTaper(envelope);
        const float rawEnv = env.process(x * (5.2f + 19.0f * dTaper));
        float sweep = std::pow(clamp01(rawEnv * (3.2f + 9.4f * eTaper)), 0.56f);
        sweep = sweep * sweep * (3.0f - 2.0f * sweep);

        const float envMix = eTaper;
        const float pos = bassMode > 0.5f ? (7.0f - 7.0f * sweep) : (7.0f * sweep);
        const float activeBand = advancePattern();
        float bank = 0.0f;
        float norm = 0.0f;
        for (int i = 0; i < kBandCount; ++i)
        {
            const float d = std::fabs(pos - (float)i);
            const float w = std::fmax(0.0f, 1.0f - d * 0.92f);
            const float b = bands[i].process(x);
            const float gate = patternGate(i, activeBand);
            const float weight = (0.28f + 0.72f * envMix * w) * gate;
            bank += b * weight;
            norm += weight;
        }
        if (norm > 1.0e-6f)
            bank /= norm;

        const float minHz = bassMode > 0.5f ? 130.0f : 210.0f;
        const float maxHz = bassMode > 0.5f ? 2350.0f : 5200.0f;
        const float targetCutoff = minHz * std::pow(maxHz / minHz, sweep);
        wahCutoff += 0.35f * (targetCutoff - wahCutoff);

        float wahLow = 0.0f;
        float wahBand = 0.0f;
        float wahHigh = 0.0f;
        const float wahQ = 2.8f + 7.6f * envMix + 3.0f * dTaper;
        wah.process(x, sampleRate, wahCutoff, wahQ, wahLow, wahBand, wahHigh);

        const float lpCutoff = bassMode > 0.5f
            ? (150.0f + 4100.0f * sweep)
            : (260.0f + 7000.0f * sweep);
        lp += onePoleCoeff(lpCutoff, sampleRate) * (x - lp);

        const float resonance = 1.02f + 1.30f * dTaper;
        const float wahWet = wahBand * (2.55f + 1.90f * envMix + 0.95f * dTaper)
                           + wahLow * (bassMode > 0.5f ? 0.18f : 0.06f);
        float wet = bassMode > 0.5f
            ? (wahWet * 1.05f + bank * (0.34f + 0.18f * drive) + lp * 0.10f)
            : (wahWet * 1.08f + bank * (0.30f + 0.16f * drive) + lp * 0.12f);
        wet *= resonance;

        const float dryLevel = 1.0f - 0.96f * mix;
        const float wetLevel = mix * (1.05f + 0.22f * drive);
        float y = x * dryLevel + wet * wetLevel;

        // Moog-style control paths can hit hot resonances; keep this as a soft
        // output guard rather than a distortion effect.
        const float outGain = 0.12f + 1.35f * audioTaper(output);
        y = std::tanh(y * 0.92f) * outGain;
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
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
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
