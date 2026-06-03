/*
 * AcousticSimulator - acoustic-emulator pedal for Rocksmith's
 * Pedal_AcousticEmulator. Reference: local "acoustic simulator" schematic
 * with clean op-amp filtering, a very mild FET/diode voice, and top/body EQ.
 * Rocksmith exposes Tone, MidShift, Body, and Mid, so those are the only
 * controls implemented here.
 */
#include "DistrhoPlugin.hpp"
#include "AcousticSimulatorParams.h"
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
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
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

} // namespace

class AcousticSimulatorCore
{
    float sampleRate = 48000.0f;
    float tone = kAcousticSimulatorDef[kTone];
    float midShift = kAcousticSimulatorDef[kMidShift];
    float body = kAcousticSimulatorDef[kBody];
    float mid = kAcousticSimulatorDef[kMid];

    Biquad inputHp;
    Biquad bodyPeak;
    Biquad pickupScoop;
    Biquad midShape;
    Biquad topShelf;
    Biquad airPeak;
    Biquad topLowPass;

    void updateFilters()
    {
        const float midHz = 275.0f + midShift * (3000.0f - 275.0f);

        inputHp.setHighPass(sampleRate, 52.0f + 28.0f * (1.0f - body), 0.72f);
        bodyPeak.setPeaking(sampleRate, 115.0f + 65.0f * body, 0.78f,
                            -1.8f + 7.4f * body);

        // Fixed electric-pickup de-honking. The Mid control can bring back
        // selected mids after this stage via the movable MidShift filter.
        pickupScoop.setPeaking(sampleRate, 680.0f, 0.95f, -5.8f + 1.8f * mid);
        midShape.setPeaking(sampleRate, midHz, 0.62f + 0.45f * (1.0f - mid),
                            -11.0f + 17.0f * mid);

        topShelf.setHighShelf(sampleRate, 3100.0f + 1200.0f * tone, 0.72f,
                              -3.0f + 8.5f * tone);
        airPeak.setPeaking(sampleRate, 6100.0f + 1900.0f * tone, 0.85f,
                           -1.2f + 4.4f * tone);
        topLowPass.setLowPass(sampleRate, 7800.0f + 5700.0f * tone, 0.70f);
    }

public:
    void reset()
    {
        inputHp.reset();
        bodyPeak.reset();
        pickupScoop.reset();
        midShape.reset();
        topShelf.reset();
        airPeak.reset();
        topLowPass.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    void setMidShift(float v)
    {
        midShift = clamp01(v);
        updateFilters();
    }

    void setBody(float v)
    {
        body = clamp01(v);
        updateFilters();
    }

    void setMid(float v)
    {
        mid = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        // A tiny FET/diode-style softening from the reference schematic. This
        // should feel like an acoustic sim preamp, not like overdrive.
        const float edge = std::tanh(in * (1.10f + 0.18f * tone));
        float y = in * 0.985f + edge * 0.015f;

        y = inputHp.process(y);
        y = bodyPeak.process(y);
        y = pickupScoop.process(y);
        y = midShape.process(y);
        y = topShelf.process(y);
        y = airPeak.process(y);
        y = topLowPass.process(y);

        // Keep perceived level in the same area while Body/Tone add energy.
        const float level = 0.90f - 0.06f * tone + 0.08f * (1.0f - body);
        return y * level;
    }
};

class AcousticSimulatorPlugin : public Plugin
{
    AcousticSimulatorCore left;
    AcousticSimulatorCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
        left.setMidShift(params[kMidShift]);
        right.setMidShift(params[kMidShift]);
        left.setBody(params[kBody]);
        right.setBody(params[kBody]);
        left.setMid(params[kMid]);
        right.setMid(params[kMid]);
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
    const char* getDescription() const override { return "Acoustic guitar simulator"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
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
