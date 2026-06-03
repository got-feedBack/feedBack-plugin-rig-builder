/*
 * AutoFilter - Mu-Tron III style envelope filter for Rocksmith's
 * Pedal_AutoFilter.
 *
 * Local references: pedals/auto filter.gif and pedals/auto filter_2.gif. The
 * schematic/layout show the Mu-Tron/Neutron topology: op-amp preamp, envelope
 * detector, LED/LDR sweep cells, peak control and selectable LP/BP/HP filter.
 * Rocksmith exposes FilterType, Res, Sens, Attack and Release, so gain/range
 * and direction are internally calibrated.
 */
#include "DistrhoPlugin.hpp"
#include "AutoFilterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.44f;
    return std::fmax(18.0f, std::fmin(hz, nyquist));
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    ms = std::fmax(0.05f, ms);
    return 1.0f - std::exp(-1.0f / (0.001f * ms * sr));
}

class OnePole
{
    float a = 0.0f;
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    void setLowPass(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        a = 1.0f - std::exp(-2.0f * kPi * hz / sr);
    }

    float process(float x)
    {
        z += a * (x - z);
        return z;
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
        q = std::fmax(0.42f, std::fmin(q, 14.0f));

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

} // namespace

class AutoFilterCore
{
    float sampleRate = 48000.0f;
    float filterType = kAutoFilterDef[kFilterType];
    float res = kAutoFilterDef[kRes];
    float sens = kAutoFilterDef[kSens];
    float attack = kAutoFilterDef[kAttack];
    float release = kAutoFilterDef[kRelease];

    OnePole inputHpDc;
    OnePole preTone;
    OnePole postTone;
    Svf filter;

    float dcIn = 0.0f;
    float env = 0.0f;
    float opto = 0.0f;
    float lastCutoff = 450.0f;

    float envAttackA = 0.0f;
    float envReleaseA = 0.0f;
    float optoAttackA = 0.0f;
    float optoReleaseA = 0.0f;

    void updateFilters()
    {
        preTone.setLowPass(sampleRate, 8800.0f - 2600.0f * res);
        postTone.setLowPass(sampleRate, 7600.0f - 2200.0f * res);

        // Rocksmith stores Attack/Release as large raw values. The JSON maps
        // them through /1000 so 128 becomes 0.128, which lands in useful
        // Mu-Tron-style envelope times here.
        const float atk = smoothstep(attack);
        const float rel = smoothstep(release);
        envAttackA = onePoleCoeffMs(1.2f + 155.0f * atk, sampleRate);
        envReleaseA = onePoleCoeffMs(24.0f + 720.0f * rel, sampleRate);

        // LED/LDR cells lag behind the detector; that little smear is what
        // keeps the sweep vocal instead of sounding like a sterile EQ follow.
        optoAttackA = onePoleCoeffMs(4.0f + 105.0f * atk, sampleRate);
        optoReleaseA = onePoleCoeffMs(70.0f + 480.0f * rel, sampleRate);
    }

    int modeIndex() const
    {
        if (filterType < 0.25f)
            return 0; // low-pass
        if (filterType < 0.75f)
            return 1; // band-pass
        return 2;     // high-pass
    }

    void updateEnvelope(float x)
    {
        const float drive = 1.3f + 18.0f * std::pow(clamp01(sens), 1.25f);
        const float detector = clamp01(std::fabs(x) * drive);
        const float envA = detector > env ? envAttackA : envReleaseA;
        env += envA * (detector - env);

        const float optoTarget = smoothstep(env);
        const float optoA = optoTarget > opto ? optoAttackA : optoReleaseA;
        opto += optoA * (optoTarget - opto);
    }

public:
    void reset()
    {
        inputHpDc.reset();
        preTone.reset();
        postTone.reset();
        filter.reset();
        dcIn = env = opto = 0.0f;
        lastCutoff = 450.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setFilterType(float v)
    {
        filterType = clamp01(v);
    }

    void setRes(float v)
    {
        res = clamp01(v);
        updateFilters();
    }

    void setSens(float v)
    {
        sens = clamp01(v);
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

    float process(float in)
    {
        // Simple input high-pass/DC blocker without a biquad allocation.
        dcIn += 0.0009f * (in - dcIn);
        float x = in - dcIn;
        x = preTone.process(x);

        const float inputGain = 1.05f + 1.15f * sens;
        const float pre = softClip(x * inputGain);
        updateEnvelope(pre);

        const int mode = modeIndex();
        const float s = smoothstep(sens);
        const float r = smoothstep(res);

        float minHz = 85.0f;
        float maxHz = 2700.0f;
        if (mode == 1)
        {
            minHz = 150.0f;
            maxHz = 3600.0f;
        }
        else if (mode == 2)
        {
            minHz = 240.0f;
            maxHz = 5200.0f;
        }

        const float sweep = smoothstep(opto * (0.66f + 0.72f * s));
        const float cutoff = minHz * std::pow(maxHz / minHz, sweep);
        lastCutoff += 0.30f * (cutoff - lastCutoff);

        const float q = 0.58f + 8.2f * r + 3.0f * r * s;
        float low = 0.0f;
        float band = 0.0f;
        float high = 0.0f;
        filter.process(pre, sampleRate, lastCutoff, q, low, band, high);

        float wet = low;
        if (mode == 1)
            wet = band * (1.25f + 0.55f * r);
        else if (mode == 2)
            wet = high * (0.90f + 0.45f * r);

        // The real pedal is buffered and not fully wet. A small dry path keeps
        // low-res presets musical while high Sens/Res still quacks hard.
        const float dryLeak = 0.18f - 0.08f * r;
        wet = wet * (1.12f + 0.42f * s + 0.28f * r) + pre * dryLeak;
        wet = postTone.process(wet);

        const float level = 0.88f / (1.0f + 0.24f * r);
        return softClip(wet * level) * 0.98f;
    }
};

class AutoFilterPlugin : public Plugin
{
    AutoFilterCore left;
    AutoFilterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setFilterType(params[kFilterType]);
        right.setFilterType(params[kFilterType]);
        left.setRes(params[kRes]);
        right.setRes(params[kRes]);
        left.setSens(params[kSens]);
        right.setSens(params[kSens]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
        left.setRelease(params[kRelease]);
        right.setRelease(params[kRelease]);
    }

public:
    AutoFilterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAutoFilterDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AutoFilter"; }
    const char* getDescription() const override { return "Mu-Tron III style envelope filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 't', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAutoFilterNames[index];
        parameter.symbol = kAutoFilterSymbols[index];
        parameter.ranges.min = kAutoFilterMin[index];
        parameter.ranges.max = kAutoFilterMax[index];
        parameter.ranges.def = kAutoFilterDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoFilterPlugin)
};

Plugin* createPlugin()
{
    return new AutoFilterPlugin();
}

END_NAMESPACE_DISTRHO
