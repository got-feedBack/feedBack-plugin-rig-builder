#ifndef GERMANIUM_DRIVE_CORE_H
#define GERMANIUM_DRIVE_CORE_H

#include <cmath>
#include "../../_shared/semiconductors.hpp"

namespace germaniumdrive {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    v = clamp01(v);
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / s;
        a = rc / (rc + dt);
    }

    void setHz(float sr, float hz)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float clampedHz = hz < 10.0f ? 10.0f : (hz > 0.45f * s ? 0.45f * s : hz);
        const float rc = 1.0f / (2.0f * kPi * clampedHz);
        const float dt = 1.0f / s;
        a = rc / (rc + dt);
    }

    void reset() { x1 = y1 = 0.0f; }

    inline float process(float x)
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
    void setHz(float sr, float hz)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float clampedHz = hz < 20.0f ? 20.0f : (hz > 0.45f * s ? 0.45f * s : hz);
        a = 1.0f - std::exp(-2.0f * kPi * clampedHz / s);
    }

    void reset() { y = 0.0f; }

    inline float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

class DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset() { x1 = y1 = 0.0f; }

    inline float process(float x)
    {
        const float y = x - x1 + 0.9975f * y1;
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class GermaniumClipper
{
    // 2N404A/MP20-like germanium junction approximation. The high Is keeps the
    // forward knee around the soft germanium region instead of silicon clipping.
    rbcomponents::DiodeSpec spec = rbcomponents::junction2N404A();
    float rSeries = 1500.0f;
    float v = 0.0f;

public:
    void reset() { v = 0.0f; }

    void setSeriesResistance(float rOhm)
    {
        rSeries = rOhm < 470.0f ? 470.0f : rOhm;
    }

    inline float process(float vin)
    {
        for (int i = 0; i < 8; ++i)
        {
            const float nVt = spec.ideality * 0.02585f;
            const float e = rbcomponents::rbClamp(v / nVt, -20.0f, 20.0f);
            const float sh = std::sinh(e);
            const float ch = std::cosh(e);
            const float f = (v - vin) / rSeries + 2.0f * spec.isAmp * sh;
            const float fp = 1.0f / rSeries + 2.0f * spec.isAmp * ch / nVt;
            v -= f / fp;
            v = rbcomponents::rbClamp(v, -spec.maxAbsV, spec.maxAbsV);
        }
        return v;
    }
};

class GermaniumDriveCore
{
    // Aion Skywave / Hudson Broadcast anchors from pedals/germanium drive.pdf:
    // Q1 2N5088 silicon class-A stage, Q2 2N404A germanium stage, C2 1 uF
    // input film cap, Low Cut 10 kA, Gain 250 kA, Level 100 kA, SPDT center-off
    // Gain Mode, 9/18/24 V Voltage slide, and TY-141P output transformer.
    static constexpr float kC2 = 1.0e-6f;
    static constexpr float kRpd = 1000000.0f;

    float sampleRate = 48000.0f;
    float gain = 0.35f;
    float lowCut = 0.45f;
    float gainMode = 0.5f;
    float voltage = 0.5f;

    RcHighPass inputC2;
    RcHighPass lowCutFilter;
    RcHighPass transformerHighPass;
    RcLowPass q1MillerPole;
    RcLowPass transformerPole;
    GermaniumClipper germanium;
    DcBlock geDc;
    DcBlock transformerDc;

    void updateComponentValues()
    {
        const float v = quantize3(voltage);
        const float mode = quantize3(gainMode);
        inputC2.setRC(sampleRate, kRpd, kC2);
        lowCutFilter.setHz(sampleRate, 28.0f + 245.0f * std::pow(clamp01(lowCut), 1.35f));
        transformerHighPass.setHz(sampleRate, 200.0f);
        q1MillerPole.setHz(sampleRate, 11500.0f - 1300.0f * mode);
        transformerPole.setHz(sampleRate, 15000.0f - 1800.0f * (1.0f - v));
        germanium.setSeriesResistance(2100.0f - 900.0f * clamp01(gain));
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        inputC2.reset();
        lowCutFilter.reset();
        transformerHighPass.reset();
        q1MillerPole.reset();
        transformerPole.reset();
        germanium.reset();
        geDc.reset();
        transformerDc.reset();
        updateComponentValues();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateComponentValues();
    }

    void setLowCut(float v)
    {
        lowCut = clamp01(v);
        updateComponentValues();
    }

    void setGainMode(float v)
    {
        gainMode = quantize3(v);
        updateComponentValues();
    }

    void setVoltage(float v)
    {
        voltage = quantize3(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float g = clamp01(gain);
        const float mode = quantize3(gainMode);
        const float volt = quantize3(voltage);

        float x = inputC2.process(0.96f * in);
        x = lowCutFilter.process(x);

        const float modeGain = 0.68f + 0.74f * mode + 0.70f * mode * mode;
        float q1 = x * (1.55f + 6.7f * g + 3.2f * g * g) * modeGain;
        q1 = q1MillerPole.process(std::tanh(0.46f * q1) * 1.72f);

        const float starveDrive = 1.42f - 0.42f * volt;
        const float starveCeil = 0.62f + 0.38f * volt;
        float q2 = germanium.process(q1 * (1.0f + 1.65f * g) * starveDrive + 0.12f);
        q2 = geDc.process(q2) * (2.25f * starveCeil);

        float transformer = std::tanh(q2 * (0.78f + 0.52f * g + 0.22f * mode));
        transformer = transformerDc.process(transformer);
        transformer = transformerHighPass.process(transformer);

        const float open = transformer;
        const float rounded = transformerPole.process(transformer);
        const float y = rounded + (open - rounded) * (0.18f + 0.82f * volt);

        const float makeup = (0.36f + 0.24f * std::exp(-g / 0.35f)) / (0.88f + 0.28f * mode);
        return std::tanh(y * makeup);
    }
};

} // namespace germaniumdrive

#endif // GERMANIUM_DRIVE_CORE_H
