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

// Musitronics Mu-Tron III — circuit-real model (schematic S/N 02050 + R.G. Keen
// verified layout). A 2-pole STATE-VARIABLE FILTER whose two integrator resistors
// are PHOTOCELLS (LDRs) swept by an envelope-driven LED (NOT an OTA filter). The
// MODE switch hard-selects the LP / BP / HP node. PEAK = the SVF damping feedback
// (resonance/Q). RANGE swaps the integrator caps (two sweep bands). DIRECTION
// inverts the control voltage (filter opens vs closes on attack). GAIN = envelope
// sensitivity only (drives the detector, never the audio level).
//
// The smooth "vowel" sweep is the photocell lag, modelled as a 2-stage detector:
// a fast electronic rectifier (~3/15 ms) feeding the slow LED→LDR optocoupler
// (NSL-32: rise TR≈55 ms, decay TD≈80–90 ms). That lag IS the smoothing — no
// cosmetic smoothstep/pow shaping, no pre/post tone LPF, no dry leak.
class AutoFilterCore
{
    float sampleRate = 48000.0f;
    float gain = kAutoFilterDef[kGain];
    float peak = kAutoFilterDef[kPeak];
    float mode = kAutoFilterDef[kMode];
    float range = kAutoFilterDef[kRange];
    float direction = kAutoFilterDef[kDirection];

    Svf filter;

    float dcIn = 0.0f;
    float env = 0.0f;     // fast electronic full-wave rectifier
    float opto = 0.0f;    // slow LED→LDR photocell (the Mu-Tron lag)
    float lastCutoff = 300.0f;

    float rectAtkA = 0.0f, rectRelA = 0.0f;
    float optoAtkA = 0.0f, optoRelA = 0.0f;

    void updateCoeffs()
    {
        // Fixed time constants — a real Mu-Tron III has NO attack/release pots.
        rectAtkA = onePoleCoeffMs(3.0f, sampleRate);    // precision rectifier, fast
        rectRelA = onePoleCoeffMs(15.0f, sampleRate);
        optoAtkA = onePoleCoeffMs(55.0f, sampleRate);   // NSL-32 rise time TR
        optoRelA = onePoleCoeffMs(90.0f, sampleRate);   // NSL-32 decay TD (+ CdS tail)
    }

    int modeIndex() const
    {
        if (mode < 0.25f)
            return 0; // low-pass
        if (mode < 0.75f)
            return 1; // band-pass
        return 2;     // high-pass
    }

public:
    void reset()
    {
        filter.reset();
        dcIn = env = opto = 0.0f;
        lastCutoff = 300.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v)      { gain = clamp01(v); }
    void setPeak(float v)      { peak = clamp01(v); }
    void setMode(float v)      { mode = clamp01(v); }
    void setRange(float v)     { range = clamp01(v); }
    void setDirection(float v) { direction = clamp01(v); }

    float process(float in)
    {
        // input DC blocker (~7 Hz one-pole high-pass)
        dcIn += 0.0009f * (in - dcIn);
        const float x = in - dcIn;

        // --- envelope detector (GAIN = sensitivity only) ---
        const float detDrive = 1.0f + 9.0f * gain;            // input sensitivity into the rectifier
        const float rect = std::fabs(x) * detDrive;
        env += (rect > env ? rectAtkA : rectRelA) * (rect - env);
        // LED → LDR photocell lag: the smooth Mu-Tron "vowel" sweep
        const float optoTarget = clamp01(env);
        opto += (optoTarget > opto ? optoAtkA : optoRelA) * (optoTarget - opto);

        // sweep 0..1; DIRECTION DOWN inverts the CV (filter closes on attack)
        float sweep = clamp01(opto);
        if (direction < 0.5f)
            sweep = 1.0f - sweep;

        // RANGE = integrator-cap swap -> two log sweep bands (from R*C + LDR window)
        const bool highRange = range >= 0.5f;
        const float fcMin = highRange ? 200.0f : 100.0f;
        const float fcMax = highRange ? 4000.0f : 1800.0f;
        const float cutoff = fcMin * std::pow(fcMax / fcMin, sweep);
        lastCutoff += 0.5f * (cutoff - lastCutoff);           // light de-zipper (LDR already smooth)

        // PEAK = SVF damping/feedback = resonance. Log taper, ~0.7 (flat) -> ~18 (quack).
        const float q = 0.7f * std::pow(26.0f, peak);

        float low = 0.0f, band = 0.0f, high = 0.0f;
        filter.process(x, sampleRate, lastCutoff, q, low, band, high);

        // MODE = clean node select (LP / BP / HP). Per-mode makeup level-matches the
        // three nodes at low Peak (a high-Q SVF BP/HP peak harder than the LP DC unity).
        const int m = modeIndex();
        float wet;
        if (m == 0)
            wet = low * 1.05f;
        else if (m == 1)
            wet = band * 1.35f;       // band-pass = the signature Mu-Tron quack
        else
            wet = high * 0.85f;

        // clean output buffer + soft safety clip on the resonant peaks
        return softClip(wet * 0.92f);
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
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
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
