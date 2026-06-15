/*
 * BobFilter - Moogerfooger/MuRF-inspired analog filter for the game's
 * Pedal_BobFilter. The local schematic shows a direct VCA and 8 FILT/VCA
 * cells under control voltage. the game exposes only Sens, Attack, Release,
 * Mix, and Filter, so this approximates the circuit as an envelope-controlled
 * 8-band resonant filter bank plus a smooth analog sweep voice.
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
    float sens = kBobFilterDef[kSens];
    float attack = kBobFilterDef[kAttack];
    float release = kBobFilterDef[kRelease];
    float mix = kBobFilterDef[kMix];
    float filter = kBobFilterDef[kFilter];

    Biquad bands[kBandCount];
    EnvelopeFollower env;

    float lp = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float hpA = 0.0f;

    void updateFilters()
    {
        static const float centers[kBandCount] = {
            170.0f, 250.0f, 365.0f, 540.0f,
            800.0f, 1180.0f, 1750.0f, 2600.0f,
        };
        const float q = 5.8f + 5.8f * sens;
        for (int i = 0; i < kBandCount; ++i)
            bands[i].setBandPass(sampleRate, centers[i], q);

        const float attackMs = 1.0f + attack * 249.0f;
        const float releaseMs = 10.0f + release * 990.0f;
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
        lp = hpX1 = hpY1 = 0.0f;
        env.reset();
        for (int i = 0; i < kBandCount; ++i)
            bands[i].reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setSens(float v)
    {
        sens = clamp01(v);
        updateFilters();
    }

    void setAttack(float v)
    {
        attack = clamp01(v);
        updateFilters();
    }

    void setRelease(float v)
    {
        release = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setFilter(float v)
    {
        filter = clamp01(v);
    }

    float process(float in)
    {
        const float x = highPass(in);
        const float mode = filter >= 0.5f ? 1.0f : 0.0f;

        const float rawEnv = env.process(x);
        float sweep = (rawEnv * (11.0f + 44.0f * sens) - 0.006f * (1.0f - sens));
        sweep = clamp01(sweep);
        sweep = std::sqrt(sweep);
        sweep = sweep * sweep * (3.0f - 2.0f * sweep);

        // Filter=0 gives an upward lowpass-style wah. Filter=1 gives the more
        // MuRF-like voiced bank, swept in the opposite direction.
        const float pos = mode > 0.5f ? (7.0f - 7.0f * sweep) : (7.0f * sweep);
        float bank = 0.0f;
        float norm = 0.0f;
        for (int i = 0; i < kBandCount; ++i)
        {
            const float d = std::fabs(pos - (float)i);
            const float w = std::fmax(0.0f, 1.0f - d * 0.92f);
            const float b = bands[i].process(x);
            bank += b * w;
            norm += w;
        }
        if (norm > 1.0e-6f)
            bank /= norm;

        const float cutoff = mode > 0.5f
            ? (220.0f + 6200.0f * sweep)
            : (90.0f + 5400.0f * sweep);
        lp += onePoleCoeff(cutoff, sampleRate) * (x - lp);

        const float resonance = 1.08f + 1.75f * sens;
        float wet = mode > 0.5f
            ? (bank * (2.15f + 1.25f * sens) + lp * 0.18f)
            : (lp * 0.62f + bank * (1.35f + 0.80f * sens));
        wet *= resonance;

        const float dryLevel = 1.0f - 0.96f * mix;
        const float wetLevel = mix * (1.10f + 0.25f * sens);
        float y = x * dryLevel + wet * wetLevel;

        // Moog-style control paths can hit hot resonances; keep this as a soft
        // output guard rather than a distortion effect.
        y = std::tanh(y * 0.92f) * 0.98f;
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
        left.setSens(params[kSens]);
        right.setSens(params[kSens]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
        left.setRelease(params[kRelease]);
        right.setRelease(params[kRelease]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setFilter(params[kFilter]);
        right.setFilter(params[kFilter]);
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
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
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
