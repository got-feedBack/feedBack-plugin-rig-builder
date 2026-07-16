/*
 * AcousticSimulator - acoustic image processor.
 *
 * The short FIR image is identified from aligned local DI/reference renders.
 * Brightness and the coupled Thickness/Amount controls interpolate the
 * measured responses; Volume remains an independent output control.
 */
#include "DistrhoPlugin.hpp"
#include "AcousticSimulatorParams.h"
#include "AcousticImageFIR.h"
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

} // namespace

class AcousticSimulatorCore
{
    // The main image is a short measured FIR. Thickness/Amount add only the
    // residual body colour that differs from the full-bright reference.
    static constexpr int kMaxRuntimeTaps = 512;
    float sampleRate = 48000.0f;
    float brightness = kAcousticSimulatorDef[kBrightness];
    float thickness = kAcousticSimulatorDef[kThickness];
    float amount = kAcousticSimulatorDef[kAmount];
    float volume = kAcousticSimulatorDef[kVolume];
    Biquad boxControl;
    float coefficients[kMaxRuntimeTaps] = {};
    float history[kMaxRuntimeTaps] = {};
    int firLength = kAcousticImageTaps;
    int historyIndex = 0;

    static float sinc(float x)
    {
        if (std::fabs(x) < 1.0e-6f)
            return 1.0f;
        const float px = kPi * x;
        return std::sin(px) / px;
    }

    static float interpolateTap(const float* source, float position)
    {
        const int centre = (int)std::floor(position);
        float value = 0.0f;
        for (int k = centre - 7; k <= centre + 8; ++k)
        {
            if (k < 0 || k >= kAcousticImageTaps)
                continue;
            const float distance = position - (float)k;
            if (std::fabs(distance) >= 8.0f)
                continue;
            value += source[k] * sinc(distance) * sinc(distance / 8.0f);
        }
        return value;
    }

    void updateModel()
    {
        const float bodyControl = clamp01(thickness) * clamp01(amount);
        const float brightnessWeight = std::pow(clamp01(brightness), 2.15f);
        const float bodyWeight = (1.0f - brightnessWeight)
                               * std::pow(bodyControl, 4.0f);
        const float imageWeight = brightnessWeight + bodyWeight;

        float source[kAcousticImageTaps];
        for (int i = 0; i < kAcousticImageTaps; ++i)
            source[i] = kAcousticBase48k[i]
                      + imageWeight * (kAcousticBright48k[i] - kAcousticBase48k[i]);

        const float rateRatio = sampleRate / 48000.0f;
        firLength = (int)std::ceil(kAcousticImageTaps * rateRatio);
        if (firLength < 1) firLength = 1;
        if (firLength > kMaxRuntimeTaps) firLength = kMaxRuntimeTaps;
        const float amplitudeScale = 1.0f / rateRatio;
        for (int i = 0; i < firLength; ++i)
            coefficients[i] = amplitudeScale * interpolateTap(source, (float)i / rateRatio);
        for (int i = firLength; i < kMaxRuntimeTaps; ++i)
            coefficients[i] = 0.0f;

        const float bodyColorDb = -8.0f * bodyControl * (1.0f - bodyControl)
                                + 1.8f * bodyControl * bodyControl;
        boxControl.setPeaking(sampleRate, 560.0f + 90.0f * clamp01(thickness),
                              2.20f, bodyColorDb);
    }

public:
    void reset()
    {
        boxControl.reset();
        for (int i = 0; i < kMaxRuntimeTaps; ++i)
            history[i] = 0.0f;
        historyIndex = 0;
        updateModel();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setBrightness(float v) { brightness = clamp01(v); updateModel(); }
    void setThickness(float v)  { thickness = clamp01(v); updateModel(); }
    void setAmount(float v)     { amount = clamp01(v); updateModel(); }
    void setVolume(float v)     { volume = clamp01(v); }

    float process(float in)
    {
        history[historyIndex] = in;
        float y = 0.0f;
        int readIndex = historyIndex;
        for (int i = 0; i < firLength; ++i)
        {
            y += coefficients[i] * history[readIndex];
            if (--readIndex < 0)
                readIndex = kMaxRuntimeTaps - 1;
        }
        if (++historyIndex >= kMaxRuntimeTaps)
            historyIndex = 0;

        y = boxControl.process(y);
        const float vol = dbToGain(-6.0f + 18.0f * audioTaper(volume));
        return y * vol;
    }
};

class AcousticSimulatorPlugin : public Plugin
{
    AcousticSimulatorCore left;
    AcousticSimulatorCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setBrightness(params[kBrightness]);
        right.setBrightness(params[kBrightness]);
        left.setThickness(params[kThickness]);
        right.setThickness(params[kThickness]);
        left.setAmount(params[kAmount]);
        right.setAmount(params[kAmount]);
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
    uint32_t getVersion() const override { return d_version(1, 5, 0); }
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
