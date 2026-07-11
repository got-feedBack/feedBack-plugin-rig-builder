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
    float sampleRate = 48000.0f;
    float gain = kAcousticSimulatorDef[kGain];
    float top = kAcousticSimulatorDef[kTop];
    float body = kAcousticSimulatorDef[kBody];
    float volume = kAcousticSimulatorDef[kVolume];

    RcHighPass inputC1;
    RcHighPass gainC6;
    RcHighPass voiceC10;
    RcHighPass voiceC11;
    RcHighPass outputC18;
    RcLowPass ic1aFeedbackC3;
    RcLowPass ic1aBrightC4;
    RcLowPass ic1bFeedbackC5;
    RcLowPass diodeStorageC7;
    RcLowPass tl061TrimC;
    RcLowPass outputFeedbackC17;
    RcLowPass outputLoad;
    Biquad ic2aScoop;
    Biquad ic2bTopBand;
    Biquad ic3aBodyBand;
    Biquad pickupScoop;
    Biquad topShelf;
    Biquad finalLowPass;

    // ── acoustic BODY model (what the AC-2 circuit can't do) ─────────────
    // A real acoustic body is a dense set of NARROW resonant modes, not a wide
    // EQ band. 12 parallel high-Q bandpass resonators at published dreadnought
    // modal frequencies (A0 Helmholtz ~100 Hz, T(1,1) top ~196, back ~226, then
    // the modal comb) with alternating polarity (physical modes alternate
    // phase) — this is what makes it "sing" hollow/woody instead of filtered.
    static constexpr int kModes = 12;
    Biquad modeBank[kModes];
    ShortAllpass boxAp1, boxAp2;
    RcLowPass boxAirLP;

    rbshared::OpAmpStage ic1a;
    rbshared::OpAmpStage ic1b;
    rbshared::OpAmpStage ic2a;
    rbshared::OpAmpStage ic2b;
    rbshared::OpAmpStage ic3a;
    rbshared::OpAmpStage ic3b;
    rbshared::OpAmpStage ic4a;
    rbshared::OpAmpStage ic5;

    rbcomponents::JfetSpec q1 = rbcomponents::jfet2N4339();
    float diodeMemory = 0.0f;
    float trim1 = 0.56f;

    void updateFilters()
    {
        const float g = audioTaper(gain);
        const float t = audioTaper(top);
        const float b = audioTaper(body);

        inputC1.setRC(sampleRate, 2200000.0f, 10.0e-9f);
        gainC6.setRC(sampleRate, 22000.0f + 3300.0f, 10.0e-6f);
        voiceC10.setRC(sampleRate, 75000.0f, 4.7e-9f);
        voiceC11.setRC(sampleRate, 4700.0f, 4.7e-9f);
        outputC18.setRC(sampleRate, 100000.0f, 1.0e-6f);

        ic1aFeedbackC3.setRC(sampleRate, 11000.0f + 4700.0f, 33.0e-9f);
        ic1aBrightC4.setRC(sampleRate, 6800.0f + 3300.0f, 3.9e-9f);
        ic1bFeedbackC5.setRC(sampleRate, 470000.0f + 22000.0f, 100.0e-12f);
        diodeStorageC7.setRC(sampleRate, 1000000.0f, 330.0e-9f);
        tl061TrimC.setRC(sampleRate, 25000.0f + 12000.0f, 1.0e-9f);
        outputFeedbackC17.setRC(sampleRate, 100000.0f, 220.0e-12f);
        outputLoad.setHz(sampleRate, 15500.0f - 2600.0f * t);

        ic2aScoop.setPeaking(sampleRate, 560.0f + 190.0f * g, 0.72f, -5.9f + 1.2f * g);
        pickupScoop.setPeaking(sampleRate, 920.0f, 0.86f, -4.4f + 1.6f * b);
        ic2bTopBand.setPeaking(sampleRate, 2700.0f + 2200.0f * t, 0.66f, -3.0f + 8.8f * t);
        ic3aBodyBand.setPeaking(sampleRate, 118.0f + 92.0f * b, 0.72f, -2.8f + 8.6f * b);
        topShelf.setHighShelf(sampleRate, 3900.0f + 1200.0f * t, 0.76f, -2.6f + 7.4f * t);
        finalLowPass.setLowPass(sampleRate, 11800.0f + 3600.0f * t, 0.68f);

        // Dreadnought modal set {Hz, Q}. A0/T(1,1)/back strong and ringy; the
        // comb above thins out (lower Q, lower gain — see kModeGain in process).
        static const float modeF[kModes] = { 100.0f, 196.0f, 226.0f, 292.0f,
                                             388.0f, 466.0f, 556.0f, 672.0f,
                                             792.0f, 918.0f, 1088.0f, 1284.0f };
        static const float modeQ[kModes] = { 16.0f, 22.0f, 25.0f, 20.0f,
                                             18.0f, 16.0f, 14.0f, 13.0f,
                                             12.0f, 11.0f, 10.0f, 9.0f };
        for (int i = 0; i < kModes; ++i)
            modeBank[i].setBandPass(sampleRate, modeF[i], modeQ[i]);
        boxAp1.init(sampleRate, 3.7f, 0.55f);
        boxAp2.init(sampleRate, 6.1f, 0.50f);
        boxAirLP.setHz(sampleRate, 4200.0f);
    }

    float jfet2N4339Vcr(float x) const
    {
        const float g = audioTaper(gain);
        const float vgsOff = 0.5f * (std::fabs(q1.vgsOffMinV) + std::fabs(q1.vgsOffMaxV));
        const float gateBias = -vgsOff * (0.72f - 0.48f * g);
        const float vgs = gateBias - 0.10f * std::fabs(x);
        const float conduct = std::pow(clamp01(1.0f + vgs / vgsOff), 2.0f);
        const float rds = q1.rdsOnMaxOhm + (270000.0f * (1.0f - conduct));
        const float divider = 3900000.0f / (3900000.0f + rds);
        return x * divider * (0.72f + 0.28f * conduct);
    }

    float diodeLadder(float x)
    {
        const float unitPerVolt = 1.0f / 3.0f;
        const float silicon = 0.62f * unitPerVolt;
        const float schottky = 0.36f * unitPerVolt;
        const float green = rbcomponents::greenLed5mm().maxAbsV * unitPerVolt;

        const float posThreshold = green + 4.0f * silicon;
        const float negThreshold = schottky + silicon;
        const float drive = x * (1.85f + 1.65f * audioTaper(gain));
        const float pos = softExcess(drive, posThreshold);
        const float neg = softExcess(-drive, negThreshold);
        const float clamped = drive - 0.34f * pos + 0.22f * neg;
        diodeMemory = diodeStorageC7.process(clamped);
        return 0.58f * x + 0.42f * diodeMemory;
    }

    float voiceBranch(float x)
    {
        float fet = jfet2N4339Vcr(x);
        float servo = ic5.process(tl061TrimC.process(fet * (0.9f + 3.5f * trim1)), 42.0f);
        servo = diodeLadder(servo);
        const float voiced = voiceC10.process(servo);
        return voiceC11.process(voiced);
    }

public:
    void reset()
    {
        inputC1.reset();
        gainC6.reset();
        voiceC10.reset();
        voiceC11.reset();
        outputC18.reset();
        ic1aFeedbackC3.reset();
        ic1aBrightC4.reset();
        ic1bFeedbackC5.reset();
        diodeStorageC7.reset();
        tl061TrimC.reset();
        outputFeedbackC17.reset();
        outputLoad.reset();
        ic2aScoop.reset();
        ic2bTopBand.reset();
        ic3aBodyBand.reset();
        pickupScoop.reset();
        topShelf.reset();
        finalLowPass.reset();
        for (int i = 0; i < kModes; ++i) modeBank[i].reset();
        boxAp1.reset();
        boxAp2.reset();
        boxAirLP.reset();
        ic1a.reset();
        ic1b.reset();
        ic2a.reset();
        ic2b.reset();
        ic3a.reset();
        ic3b.reset();
        ic4a.reset();
        ic5.reset();
        diodeMemory = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        ic1a.setSpec(rbshared::tl072Spec());
        ic1b.setSpec(rbshared::tl072Spec());
        ic2a.setSpec(rbshared::tl072Spec());
        ic2b.setSpec(rbshared::tl072Spec());
        ic3a.setSpec(rbshared::tl072Spec());
        ic3b.setSpec(rbshared::tl072Spec());
        ic4a.setSpec(rbshared::tl072Spec());
        ic5.setSpec(rbshared::tl061Spec());
        ic1a.setSampleRate(sampleRate);
        ic1b.setSampleRate(sampleRate);
        ic2a.setSampleRate(sampleRate);
        ic2b.setSampleRate(sampleRate);
        ic3a.setSampleRate(sampleRate);
        ic3b.setSampleRate(sampleRate);
        ic4a.setSampleRate(sampleRate);
        ic5.setSampleRate(sampleRate);
        reset();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateFilters();
    }

    void setTop(float v)
    {
        top = clamp01(v);
        updateFilters();
    }

    void setBody(float v)
    {
        body = clamp01(v);
        updateFilters();
    }

    void setVolume(float v)
    {
        volume = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float g = audioTaper(gain);
        const float t = audioTaper(top);
        const float b = audioTaper(body);

        float x = inputC1.process(in);
        const float ic1aLocalFb = ic1aFeedbackC3.process(x);
        const float bright = x - ic1aBrightC4.process(x);
        x = ic1a.process(x + 0.18f * bright - 0.08f * ic1aLocalFb, 3.25f);

        // VR4 is the circuit gain/drive control; keep its low end near unity
        // in the plugin because the real pedal also has a dedicated Volume pot.
        const float gainPotDb = 2.5f + 15.5f * g;
        x = gainC6.process(x * dbToGain(gainPotDb));
        x = ic1b.process(x - 0.12f * ic1bFeedbackC5.process(x), 3.1f + 8.6f * g);

        const float branch = voiceBranch(x);
        float y = x * (0.78f - 0.10f * g) + branch * (0.22f + 0.18f * g);

        y = ic2a.process(ic2aScoop.process(y), 2.4f);
        y = pickupScoop.process(y);

        const float topPath = ic2b.process(ic2bTopBand.process(y), 1.9f + 2.2f * t);

        // BODY = modal resonator bank + box air, replacing the old single wide
        // EQ band (which read as "a filter"). Alternating mode polarity +
        // per-mode gains tapering with frequency = the woody hollow comb.
        static const float kModeGain[kModes] = { 1.00f, 0.92f, 0.72f, 0.76f,
                                                 0.60f, 0.52f, 0.46f, 0.40f,
                                                 0.35f, 0.31f, 0.27f, 0.24f };
        // Signs: the strong low modes (A0/T11/back/292) add IN PHASE with the
        // dry path — strict ± alternation cancelled every other mode against
        // the dry mix and flattened them. Some inversion up high keeps the
        // comb-like character between upper modes.
        static const float kModeSign[kModes] = { 1.0f, 1.0f, 1.0f, 1.0f,
                                                 1.0f, -1.0f, 1.0f, -1.0f,
                                                 1.0f, -1.0f, 1.0f, -1.0f };
        float res = 0.0f;
        for (int i = 0; i < kModes; ++i)
            res += kModeSign[i] * kModeGain[i] * modeBank[i].process(y);
        const float air = boxAirLP.process(boxAp2.process(boxAp1.process(y)));
        const float bodyPath = ic3a.process(res * 3.4f + air * 0.7f, 1.4f + 1.6f * b);

        const float mixed = y * 0.34f
                          + topPath * (0.16f + 0.28f * t)
                          + bodyPath * (0.30f + 0.50f * b);

        y = ic3b.process(mixed, 2.0f);
        y = topShelf.process(y);
        y = finalLowPass.process(y);

        // Keep Gain as a voicing/drive control. Output loudness belongs to the
        // Volume pot, so compensate the low end of VR4's range before Volume.
        const float gainMakeupDb = 8.5f * std::pow(1.0f - gain, 2.15f);
        y *= dbToGain(gainMakeupDb);

        // Volume range raised: the old -24..+4 dB span left the pinned Volume
        // (0.62) ~11 dB below unity (output -39 dBFS = near-inaudible in a mix).
        // Now 0.62 lands ~-16 dBFS, in line with the other pedals.
        const float vol = dbToGain(7.0f + 11.0f * audioTaper(volume));
        const float fb = outputFeedbackC17.process(y);
        y = ic4a.process((y - 0.10f * fb) * vol, 1.0f + 3.2f * audioTaper(volume));
        y = outputLoad.process(y);
        y = outputC18.process(y);
        return std::tanh(y * 1.04f) * 0.96f;
    }
};

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
