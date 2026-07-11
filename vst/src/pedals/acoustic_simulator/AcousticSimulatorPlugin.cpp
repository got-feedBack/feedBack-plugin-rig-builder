/*
 * AcousticSimulator - blue-button acoustic pedal model.
 *
 * Reference: pedals/acoustic simulator.png. The circuit is a clean TL072
 * filter/preamp with a TL061 + 2N4339/J201 FET voice branch, 1N4148/green LED
 * diode ladder, Top/Body voicing filters and a final volume stage.
 */
#include "DistrhoPlugin.hpp"
#include "AcousticSimulatorParams.h"
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.75f);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return hz < 12.0f ? 12.0f : (hz > nyquist ? nyquist : hz);
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class RcLowPass
{
    float a = 0.0f;
    float y = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = dt / (rc + dt);
    }

    void setHz(float sr, float hz)
    {
        const float safeSr = sr > 1000.0f ? sr : 48000.0f;
        a = 1.0f - std::exp(-2.0f * kPi * clampFreq(hz, safeSr) / safeSr);
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

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
        const float invA0 = 1.0f / (std::fabs(na0) < 1.0e-12f ? 1.0f : na0);
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
        return dn(y);
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    // Constant-0dB-peak bandpass (RBJ). Narrow (high-Q) instances RING like a
    // physical resonance — the modal building block for the acoustic body.
    void setBandPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(alpha, 0.0f, -alpha,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

// Short fixed-delay allpass — the diffuse "inside the box" air. Two of these
// nested (3.7/6.1 ms) + a lowpass give the hollow early-reflection shimmer an
// acoustic body adds around the plucked string.
class ShortAllpass
{
    float buf[512] = {};
    int idx = 0;
    int len = 1;
    float g = 0.55f;

public:
    void init(float sr, float ms, float gain)
    {
        len = (int)(sr * ms * 0.001f);
        if (len < 1) len = 1;
        if (len > 512) len = 512;
        g = gain;
        reset();
    }

    void reset()
    {
        for (int i = 0; i < 512; ++i) buf[i] = 0.0f;
        idx = 0;
    }

    float process(float x)
    {
        const float d = buf[idx];
        const float y = d - g * x;
        buf[idx] = x + g * y;
        if (++idx >= len) idx = 0;
        return dn(y);
    }
};

static inline float softExcess(float x, float threshold)
{
    if (x <= threshold)
        return 0.0f;
    return std::tanh((x - threshold) * 1.7f) / 1.7f;
}

} // namespace

class AcousticSimulatorCore
{
    // ── Acoustic image processor (rebuilt from scratch) ──────────────────
    // The AC-2-style circuit (wide EQ bands + FET/diode branch) reads as "a
    // filter" — the real pedal does too. This core instead models what makes
    // an acoustic sound acoustic, the way modern sims (Aura/ToneDexter) do:
    //
    //   1. PICKUP DE-EMPHASIS  — tame the magnetic pickup's mid resonance.
    //   2. BODY                — a DENSE bank of 24 narrow modal resonators at
    //      published dreadnought frequencies (A0 Helmholtz ~98 Hz, T(1,1) top
    //      ~196, back ~226, then the modal comb thickening into a statistical
    //      plateau by ~2.5 kHz) + diffuse "inside the box" air (nested short
    //      allpasses). Narrow modes RING like wood; wide EQ never does.
    //   3. STRING SPARKLE      — soft asymmetric saturation of the top band,
    //      restoring the bronze-string zing a magnetic pickup rolls off.
    //   4. ATTACK SOFTENING    — gentle envelope compression, the mic'd-box
    //      "bloom" instead of the electric pick spike.
    //
    // Panel semantics preserved (Gain/Top/Body/Volume; RS maps Mid->Gain,
    // Tone->Top, Body->Body): Gain = drive/compression character, Top =
    // sparkle + brightness, Body = how much box.
    float sampleRate = 48000.0f;
    float gain = kAcousticSimulatorDef[kGain];
    float top = kAcousticSimulatorDef[kTop];
    float body = kAcousticSimulatorDef[kBody];
    float volume = kAcousticSimulatorDef[kVolume];

    RcHighPass inputHP;
    Biquad pickupNotch;
    Biquad pickupLP;

    static constexpr int kModes = 24;
    Biquad modeBank[kModes];
    RcHighPass bodyHP;
    ShortAllpass boxAp1, boxAp2, boxAp3;
    RcLowPass boxAirLP;

    Biquad sparkleBand;
    Biquad topShelfOut;
    Biquad finalLP;
    RcHighPass outHP;

    float env = 0.0f;
    float envAttack = 0.0f;
    float envRelease = 0.0f;

    // Dreadnought modal set {Hz}: strong ringy low modes, comb thickening up
    // high (Caldersmith/French modal surveys). Q and gain taper with index.
    static const float kModeF[kModes];
    static const float kModeQ[kModes];
    static const float kModeG[kModes];
    static const float kModeS[kModes];

    void updateFilters()
    {
        const float t = audioTaper(top);
        const float g = audioTaper(gain);

        inputHP.setRC(sampleRate, 45000.0f, 100.0e-9f);          // ~35 Hz
        // Magnetic pickup mid "spike" de-emphasis: deeper with Gain (more
        // "acoustic image", less electric character).
        pickupNotch.setPeaking(sampleRate, 2600.0f, 1.1f, -3.0f - 4.5f * g);
        pickupLP.setLowPass(sampleRate, 7800.0f + 2800.0f * t, 0.60f);

        for (int i = 0; i < kModes; ++i)
            modeBank[i].setBandPass(sampleRate, kModeF[i], kModeQ[i]);
        bodyHP.setRC(sampleRate, 33000.0f, 68.0e-9f);            // ~70 Hz, keep rumble out
        boxAp1.init(sampleRate, 3.1f, 0.55f);
        boxAp2.init(sampleRate, 5.3f, 0.52f);
        boxAp3.init(sampleRate, 8.9f, 0.45f);
        boxAirLP.setHz(sampleRate, 4600.0f);

        sparkleBand.setHighPass(sampleRate, 2500.0f, 0.707f);
        topShelfOut.setHighShelf(sampleRate, 5200.0f, 0.75f, -1.5f + 6.5f * t);
        finalLP.setLowPass(sampleRate, 10500.0f + 4000.0f * t, 0.62f);
        outHP.setRC(sampleRate, 100000.0f, 47.0e-9f);            // ~34 Hz DC guard

        envAttack = 1.0f - std::exp(-1.0f / (0.004f * sampleRate));   // 4 ms
        envRelease = 1.0f - std::exp(-1.0f / (0.180f * sampleRate));  // 180 ms
    }

public:
    void reset()
    {
        inputHP.reset();
        pickupNotch.reset();
        pickupLP.reset();
        for (int i = 0; i < kModes; ++i) modeBank[i].reset();
        bodyHP.reset();
        boxAp1.reset();
        boxAp2.reset();
        boxAp3.reset();
        boxAirLP.reset();
        sparkleBand.reset();
        topShelfOut.reset();
        finalLP.reset();
        outHP.reset();
        env = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v)   { gain = clamp01(v); updateFilters(); }
    void setTop(float v)    { top = clamp01(v); updateFilters(); }
    void setBody(float v)   { body = clamp01(v); updateFilters(); }
    void setVolume(float v) { volume = clamp01(v); updateFilters(); }

    float process(float in)
    {
        const float g = audioTaper(gain);
        const float t = audioTaper(top);
        const float b = audioTaper(body);

        // 1) input conditioning: HP + pickup de-emphasis
        float x = inputHP.process(in);
        x = pickupNotch.process(x);
        x = pickupLP.process(x);

        // 4) attack softening (feed-forward): mic'd boxes bloom, they don't
        // spike. Gentle ratio, scaled by Gain.
        const float mag = std::fabs(x);
        env += (mag > env ? envAttack : envRelease) * (mag - env);
        const float comp = 1.0f / (1.0f + (1.5f + 4.0f * g) * env);
        x *= 0.55f + 0.45f * comp;

        // 2) BODY: dense modal bank + diffuse box air
        const float xb = bodyHP.process(x);
        float res = 0.0f;
        for (int i = 0; i < kModes; ++i)
            res += kModeS[i] * kModeG[i] * modeBank[i].process(xb);
        const float air = boxAirLP.process(
            boxAp3.process(boxAp2.process(boxAp1.process(xb))));

        // 3) string sparkle: soft asymmetric saturation of the top band (adds
        // the even-harmonic bronze zing magnetic pickups lose)
        const float sp = sparkleBand.process(x + 0.6f * res);
        const float spDrive = sp * (2.2f + 2.4f * t);
        const float sparkle = std::tanh(spDrive + 0.22f * spDrive * spDrive) * 0.30f;

        // mix: direct string + body + air + sparkle
        float y = x * 0.34f
                + res * (0.55f + 0.95f * b)
                + air * (0.10f + 0.30f * b)
                + sparkle * (0.10f + 0.42f * t);

        y = topShelfOut.process(y);
        y = finalLP.process(y);
        y = outHP.process(y);

        // Makeup: the narrow modal bank passes far less broadband energy than
        // a wide-EQ chain; +19 dB lands the default at ~-15.5 dBFS RMS like
        // the other pedals (RS pins Volume at 0.62).
        y *= dbToGain(19.0f);
        const float vol = dbToGain(-6.0f + 18.0f * audioTaper(volume));
        y *= vol;
        return std::tanh(y * 1.04f) * 0.96f;
    }
};

const float AcousticSimulatorCore::kModeF[AcousticSimulatorCore::kModes] = {
     98.0f, 196.0f, 226.0f, 258.0f, 292.0f, 330.0f, 388.0f, 435.0f,
    480.0f, 556.0f, 610.0f, 672.0f, 735.0f, 800.0f, 875.0f, 950.0f,
   1040.0f, 1140.0f, 1250.0f, 1400.0f, 1600.0f, 1850.0f, 2150.0f, 2500.0f };
const float AcousticSimulatorCore::kModeQ[AcousticSimulatorCore::kModes] = {
    18.0f, 26.0f, 28.0f, 22.0f, 20.0f, 18.0f, 17.0f, 16.0f,
    15.0f, 14.0f, 13.0f, 12.5f, 12.0f, 11.5f, 11.0f, 10.5f,
    10.0f,  9.5f,  9.0f,  8.5f,  8.0f,  7.5f,  7.0f,  6.5f };
const float AcousticSimulatorCore::kModeG[AcousticSimulatorCore::kModes] = {
    1.00f, 0.95f, 0.70f, 0.55f, 0.72f, 0.50f, 0.58f, 0.42f,
    0.38f, 0.42f, 0.33f, 0.36f, 0.28f, 0.30f, 0.24f, 0.26f,
    0.22f, 0.20f, 0.19f, 0.17f, 0.16f, 0.15f, 0.14f, 0.13f };
const float AcousticSimulatorCore::kModeS[AcousticSimulatorCore::kModes] = {
    1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f,
    1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f,
    -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };

class AcousticSimulatorPlugin : public Plugin
{
    AcousticSimulatorCore left;
    AcousticSimulatorCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setTop(params[kTop]);
        right.setTop(params[kTop]);
        left.setBody(params[kBody]);
        right.setBody(params[kBody]);
        left.setVolume(params[kVolume]);
        right.setVolume(params[kVolume]);
    }

public:
    AcousticSimulatorPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAcousticSimulatorDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AcousticSimulator"; }
    const char* getDescription() const override { return "blue-button acoustic simulator"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'c', 's', 'm'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAcousticSimulatorNames[index];
        parameter.symbol = kAcousticSimulatorSymbols[index];
        parameter.ranges.min = kAcousticSimulatorMin[index];
        parameter.ranges.max = kAcousticSimulatorMax[index];
        parameter.ranges.def = kAcousticSimulatorDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcousticSimulatorPlugin)
};

Plugin* createPlugin()
{
    return new AcousticSimulatorPlugin();
}

END_NAMESPACE_DISTRHO
