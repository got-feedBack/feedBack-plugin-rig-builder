/*
 * GermaniumDrive — germanium boost/overdrive for Rocksmith's
 * Pedal_GermaniumDrive. Character reference: the Skywave (Aion) adaptation of
 * the Hudson Electronics Broadcast (pedals/germanium drive.pdf).
 *
 * The Broadcast is a HYBRID silicon + germanium, class-A boost/light-overdrive
 * with a TRANSFORMER-COUPLED output that saturates on higher drive. It is a
 * warm boost-into-light-OD, NOT a harsh fuzz. Signal path modelled here:
 *
 *   IN -> C2 input coupling (fixed low cut) -> Q1 2N5088 silicon class-A gain
 *   stage (the GAIN knob is the feedback amount fed germanium->silicon, so it
 *   raises drive) -> Q2 2N404A germanium stage (soft, asymmetric saturation =
 *   the warm even-harmonic core) -> TY-141P output transformer (even-harmonic
 *   rounding that engages with level) -> LEVEL.
 *
 * Rocksmith exposes only Gain and Tone, so:
 *   - Gain : silicon feedback drive into the germanium + transformer saturation.
 *   - Tone : post brightness (the real pedal has no treble control; this is a
 *            musical brightness tilt over the warm voicing).
 * Low Cut / Level / Gain Mode / Voltage are pinned at musical fixed values.
 *
 * Loudness: a DETERMINISTIC static makeup keyed only to the Gain knob. The
 * previous revision used an RMS-matching auto-makeup (AGC) whose ~200 ms
 * envelope swelled on every note over the compressing clipper — that is what
 * sounded like a "reverb / strange attack". A static makeup cannot swell, so
 * the pedal is just a drive again.
 */
#include "DistrhoPlugin.hpp"
#include "GermaniumDriveParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class GermaniumDriveCore
{
    float sampleRate = 48000.0f;
    float gain = kGermaniumDriveDef[kGain];
    float tone = kGermaniumDriveDef[kTone];

    // input coupling + fixed low-cut (one-pole high pass)
    float hpX1 = 0.0f, hpY1 = 0.0f, hpA = 0.0f;
    // tone brightness (one-pole low pass)
    float toneY = 0.0f, toneA = 0.0f;
    // germanium asymmetry DC corrector (precomputed)
    float germBiasDc = 0.0f;

    void updateFilters()
    {
        // C2 input coupling + Low Cut pinned tight-but-full (~85 Hz). The
        // Broadcast's Low Cut tightens the low end; this is a musical fixed
        // position so the pedal stays full without getting flabby.
        const float hpHz = 85.0f;
        const float rc = 1.0f / (2.0f * kPi * hpHz);
        const float dt = 1.0f / sampleRate;
        hpA = rc / (rc + dt);

        // Tone = brightness. Sweeps the post low-pass corner ~1.1k..~6.6k.
        const float toneHz = 1100.0f * std::pow(6.0f, tone);
        toneA = 1.0f - std::exp(-2.0f * kPi * toneHz / sampleRate);

        germBiasDc = std::tanh(0.17f);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float toneLowPass(float x)
    {
        toneY += toneA * (x - toneY);
        return toneY;
    }

public:
    void reset()
    {
        hpX1 = hpY1 = toneY = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v) { gain = clamp01(v); updateFilters(); }
    void setTone(float v) { tone = clamp01(v); updateFilters(); }

    float process(float in)
    {
        const float g = gain;

        // C2 input coupling + fixed low cut.
        float x = highPass(in);

        // Q1 — 2N5088 silicon class-A gain stage. The GAIN knob is the feedback
        // amount fed back from the germanium stage, so it raises the drive into
        // Q2. Kept mostly clean (it is a preamp) with a gentle soft ceiling so
        // the saturation character comes from germanium + transformer, not from
        // a hard silicon clip.
        const float q1Gain = 1.7f + 7.8f * g;
        float q1 = std::tanh(x * q1Gain * 0.45f) * 1.55f;

        // Q2 — 2N404A germanium stage. Soft, ASYMMETRIC saturation: the warm,
        // even-harmonic core of the Broadcast. The +0.17 bias (DC-corrected)
        // makes the two halves clip differently = germanium warmth.
        float q2 = std::tanh(q1 * (1.0f + 1.7f * g) + 0.17f) - germBiasDc;

        // TY-141P output transformer: even-harmonic rounding that engages with
        // level/drive (the transformer "saturates on higher drive levels").
        float tr = std::tanh(q2 * (0.85f + 0.5f * g));

        // Tone — post brightness tilt over the warm voicing.
        const float dark = toneLowPass(tr);
        float y = dark + (tr - dark) * (0.25f + 0.75f * tone);

        // Deterministic static makeup (function of Gain only — never of the
        // signal envelope, so it cannot swell/pump). Calibrated against the raw
        // core RMS so engaging the pedal and sweeping Gain holds a roughly
        // unity, consistent level (~0..+1.5 dB vs dry across the whole sweep).
        const float makeup = 0.13f + 1.10f * std::exp(-g / 0.22f);
        return y * makeup;
    }
};

class GermaniumDrivePlugin : public Plugin
{
    GermaniumDriveCore left;
    GermaniumDriveCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    GermaniumDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kGermaniumDriveDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "GermaniumDrive"; }
    const char* getDescription() const override { return "Classic smooth germanium overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 3); }
    int64_t getUniqueId() const override { return d_cconst('G', 'd', 'r', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kGermaniumDriveNames[index];
        parameter.symbol = kGermaniumDriveSymbols[index];
        parameter.ranges.min = kGermaniumDriveMin[index];
        parameter.ranges.max = kGermaniumDriveMax[index];
        parameter.ranges.def = kGermaniumDriveDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GermaniumDrivePlugin)
};

Plugin* createPlugin()
{
    return new GermaniumDrivePlugin();
}

END_NAMESPACE_DISTRHO
