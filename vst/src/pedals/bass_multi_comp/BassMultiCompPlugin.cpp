/*
 * BassMultiComp - multiband bass compressor for the game's Bass_Pedal_MBComp.
 * Reference: local "multi bass comp" EBS MultiComp 2-style schematic. The
 * circuit is reduced to the game's controls: Compress, Filter, and Rate.
 */
#include "DistrhoPlugin.hpp"
#include "BassMultiCompParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(std::fmax(amp, 1.0e-7f));
}

static inline float timeCoeff(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return std::exp(-1.0f / samples);
}

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

class BassMultiCompCore
{
    float sampleRate = 48000.0f;
    float compress = kBassMultiCompDef[kCompress];
    float filter = kBassMultiCompDef[kFilter];
    float rate = kBassMultiCompDef[kRate];

    float lowState = 0.0f;
    float crossoverA = 0.0f;
    float lowDc = 0.0f;
    float outDc = 0.0f;
    float hpA = 0.0f;

    // Auto-makeup state: slow-smoothed mean-square of the clean vs the
    // compressed (pre-makeup) signal. makeup = sqrt(msClean/msComp) drives
    // the compressed path back to the input's level, so raising Compress
    // keeps the OUTPUT loudness constant regardless of how much reduction
    // the bands apply (replaces the old open-loop curve that ballooned to
    // +16 dB at Compress=1).
    float msClean = 0.0f;
    float msWet = 0.0f;
    float msCoeff = 0.0f;

    EnvelopeFollower lowEnv;
    EnvelopeFollower highEnv;

    void updateFilters()
    {
        const float cutoff = 110.0f + filter * (1000.0f - 110.0f);
        crossoverA = 1.0f - std::exp(-2.0f * kPi * cutoff / sampleRate);

        const float hpHz = 28.0f;
        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * hpHz);
        hpA = rc / (rc + dt);

        // Higher Rate recovers faster. Low band is intentionally slower to
        // hold bass sustain; high band reacts quicker to pick noise.
        const float lowRelease = 520.0f - 455.0f * rate;
        const float highRelease = 320.0f - 270.0f * rate;
        lowEnv.setTimes(sampleRate, 13.0f - 7.0f * rate, lowRelease);
        highEnv.setTimes(sampleRate, 5.0f - 2.5f * rate, highRelease);

        // ~250 ms loudness window for the auto-makeup tracker — slow enough
        // not to pump on note onsets, fast enough to settle within a phrase.
        msCoeff = 1.0f - std::exp(-1.0f / std::fmax(1.0f, 0.250f * sampleRate));
    }

    float highPass(float x)
    {
        const float y = hpA * (outDc + x - lowDc);
        lowDc = x;
        outDc = y;
        return y;
    }

    float bandGain(float env, bool lowBand) const
    {
        const float c = compress;
        const float threshold = lowBand ? (-7.5f - 28.5f * c) : (-9.5f - 26.0f * c);
        const float ratio = lowBand ? (1.15f + 5.8f * c) : (1.10f + 4.4f * c);
        const float maxReduction = lowBand ? (7.0f + 15.0f * c) : (5.0f + 12.0f * c);
        const float knee = 5.0f;
        const float envDb = ampToDb(env);
        const float over = envDb - threshold;
        if (over <= -knee * 0.5f)
            return 1.0f;

        float effectiveOver = over;
        if (over < knee * 0.5f)
        {
            const float x = over + knee * 0.5f;
            effectiveOver = (x * x) / (2.0f * knee);
        }

        const float reduction = std::fmin(maxReduction, effectiveOver * (1.0f - 1.0f / ratio));
        return dbToGain(-reduction);
    }

public:
    void reset()
    {
        lowState = 0.0f;
        lowDc = outDc = 0.0f;
        msClean = msWet = 0.0f;
        lowEnv.reset();
        highEnv.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setCompress(float v)
    {
        compress = clamp01(v);
        updateFilters();
    }

    void setFilter(float v)
    {
        filter = clamp01(v);
        updateFilters();
    }

    void setRate(float v)
    {
        rate = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float clean = highPass(in);

        lowState += crossoverA * (clean - lowState);
        const float low = lowState;
        const float high = clean - low;

        const float lowGain = bandGain(lowEnv.process(low), true);
        const float highGain = bandGain(highEnv.process(high), false);
        const float compressed = low * lowGain + high * highGain;

        // Parallel component keeps attack and makes low Compress settings feel
        // like the original pedal rather than a studio limiter.
        const float dry = 0.20f * (1.0f - compress) + 0.05f;
        const float wet = clean * dry + compressed * (1.0f - dry);
        // Auto-makeup (closed loop): drive the FULL wet mix to the clean
        // signal's smoothed RMS so the OUTPUT loudness stays constant as
        // Compress rises instead of ballooning. Matches the whole mix (not
        // just the compressed band) so it can CUT as well as boost; range-
        // limited (-12..+18 dB) so silence/noise can't blow up. See
        // AMP_LOUDNESS.md.
        msClean += msCoeff * (clean * clean - msClean);
        msWet += msCoeff * (wet * wet - msWet);
        float makeup = 1.0f;
        if (msWet > 1.0e-9f && msClean > 1.0e-9f)
            makeup = std::sqrt(msClean / msWet);
        makeup = std::fmin(dbToGain(18.0f), std::fmax(dbToGain(-12.0f), makeup));
        float y = wet * makeup;

        // Very light output safety, not an audible drive stage.
        y = std::tanh(y * 0.98f) * 0.99f;
        return y;
    }
};

class BassMultiCompPlugin : public Plugin
{
    BassMultiCompCore left;
    BassMultiCompCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setCompress(params[kCompress]);
        right.setCompress(params[kCompress]);
        left.setFilter(params[kFilter]);
        right.setFilter(params[kFilter]);
        left.setRate(params[kRate]);
        right.setRate(params[kRate]);
    }

public:
    BassMultiCompPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassMultiCompDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassMultiComp"; }
    const char* getDescription() const override { return "Multiband bass compressor"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('B', 'm', 'c', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassMultiCompNames[index];
        parameter.symbol = kBassMultiCompSymbols[index];
        parameter.ranges.min = kBassMultiCompMin[index];
        parameter.ranges.max = kBassMultiCompMax[index];
        parameter.ranges.def = kBassMultiCompDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassMultiCompPlugin)
};

Plugin* createPlugin()
{
    return new BassMultiCompPlugin();
}

END_NAMESPACE_DISTRHO
