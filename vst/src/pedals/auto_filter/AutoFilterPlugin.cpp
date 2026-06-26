/*
 * AutoFilter - Mu-Tron III / Neutron style envelope filter.
 *
 * Local references: pedals/auto filter.gif and pedals/auto filter_2.gif. The
 * schematic/layout show TL072/TL074 op-amp filtering, ICL7660 negative rail,
 * dual LED/LDR sweep cells (NSL32-style), Peak control and selectable LP/BP/HP,
 * Range and Direction switches. The DSP keeps the wah resonator as the main
 * voice in every mode; Mode changes the parallel color and Direction only flips
 * the control voltage sweep.
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
    float gain = kAutoFilterDef[kGain];
    float peak = kAutoFilterDef[kPeak];
    float mode = kAutoFilterDef[kMode];
    float range = kAutoFilterDef[kRange];
    float direction = kAutoFilterDef[kDirection];

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
        const float p = smoothstep(peak);
        preTone.setLowPass(sampleRate, 9100.0f - 2600.0f * p);
        postTone.setLowPass(sampleRate, 7800.0f - 2100.0f * p);

        // NSL32-style opto: the detector charges quickly, the LDR conducts
        // slightly later and releases much more slowly than the rectifier.
        envAttackA = onePoleCoeffMs(0.8f, sampleRate);
        envReleaseA = onePoleCoeffMs(18.0f + 60.0f * (1.0f - gain), sampleRate);
        optoAttackA = onePoleCoeffMs(2.2f + 5.5f * (1.0f - range), sampleRate);
        optoReleaseA = onePoleCoeffMs(22.0f + 72.0f * (1.0f - gain), sampleRate);
    }

    int modeIndex() const
    {
        if (mode < 0.25f)
            return 0; // low-pass
        if (mode < 0.75f)
            return 1; // band-pass
        return 2;     // high-pass
    }

    void updateEnvelope(float x)
    {
        const float g = smoothstep(gain);
        const float rectified = std::fabs(x);
        const float detectorDrive = 12.0f + 48.0f * std::pow(clamp01(gain), 1.18f);
        const float detector = clamp01((1.0f - std::exp(-rectified * detectorDrive)) * (0.72f + 0.45f * g));
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

    void setGain(float v)
    {
        gain = clamp01(v);
        updateFilters();
    }

    void setPeak(float v)
    {
        peak = clamp01(v);
        updateFilters();
    }

    void setMode(float v)
    {
        mode = clamp01(v);
    }

    void setRange(float v)
    {
        range = clamp01(v);
        updateFilters();
    }

    void setDirection(float v)
    {
        direction = clamp01(v);
    }

    float process(float in)
    {
        // Simple input high-pass/DC blocker without a biquad allocation.
        dcIn += 0.0009f * (in - dcIn);
        float x = in - dcIn;
        x = preTone.process(x);

        const float g = smoothstep(gain);
        const float p = smoothstep(peak);
        const float inputGain = 0.88f + 2.15f * g;
        const float pre = softClip(x * inputGain);
        updateEnvelope(pre);

        const int mode = modeIndex();
        const bool highRange = range >= 0.5f;
        const bool upSweep = direction >= 0.5f;

        float sweep = std::pow(clamp01(opto * (1.25f + 1.35f * g)), 0.58f);
        sweep = smoothstep(sweep);
        if (!upSweep)
            sweep = 1.0f - sweep;

        const float minHz = highRange ? 115.0f : 68.0f;
        const float maxHz = highRange ? 3850.0f : 2100.0f;
        const float cutoff = minHz * std::pow(maxHz / minHz, sweep);
        lastCutoff += 0.38f * (cutoff - lastCutoff);

        const float q = 0.78f + 11.5f * p + 3.5f * p * g;
        float low = 0.0f;
        float band = 0.0f;
        float high = 0.0f;
        filter.process(pre, sampleRate, lastCutoff, q, low, band, high);

        // The Mu-Tron mode switch changes which filter node is emphasized, but
        // the musical identity is still the swept resonant LDR wah. Keep band
        // energy dominant so Direction never feels like a HP/LP type switch.
        const float wah = band * (2.35f + 2.65f * p + 0.85f * g);
        float wet = wah + low * 0.20f;
        if (mode == 0)
            wet = wah * 0.92f + low * (0.52f + 0.16f * p);
        else if (mode == 2)
            wet = wah * 0.96f + high * (0.20f + 0.18f * p);

        // The real pedal is buffered and not fully wet. A small dry path keeps
        // low-peak presets musical while high Gain/Peak still quacks hard.
        const float dryLeak = 0.095f - 0.045f * p;
        wet = wet * (1.34f + 0.64f * g + 0.42f * p) + pre * dryLeak;
        wet = postTone.process(wet);

        const float level = 0.88f / (1.0f + 0.24f * p);
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
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setPeak(params[kPeak]);
        right.setPeak(params[kPeak]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
        left.setRange(params[kRange]);
        right.setRange(params[kRange]);
        left.setDirection(params[kDirection]);
        right.setDirection(params[kDirection]);
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
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
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
