#ifndef VINTAGE_DISTORTION_CORE_H
#define VINTAGE_DISTORTION_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace vintagedistortion {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
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
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float rc = rOhm * cFarad;
        a = 1.0f - std::exp(-1.0f / (rc * s));
    }

    void setHz(float sr, float hz)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        a = 1.0f - std::exp(-2.0f * kPi * hz / s);
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
        const float y = x - x1 + 0.9985f * y1;
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class VintageDistortionCore
{
    // DOD 250 schematic from pedals/vintage distortion.png:
    // LM741, Gain 500 k, feedback R8 1 M + C5 22 pF, C3 4.7 nF,
    // asymmetric 1N4148 clipper, Volume 100 k.
    static constexpr float kC2 = 10.0e-9f;
    static constexpr float kR5 = 2200000.0f;
    static constexpr float kR6 = 10000.0f;
    static constexpr float kGainPot = 500000.0f;
    static constexpr float kR7 = 4700.0f;
    static constexpr float kC3 = 4.7e-9f;
    static constexpr float kR8 = 1000000.0f;
    static constexpr float kC5 = 22.0e-12f;
    static constexpr float kC4 = 4.7e-6f;
    static constexpr float kR9 = 10000.0f;
    static constexpr float kC6 = 1.0e-9f;
    static constexpr float kVolumePot = 100000.0f;

    float sampleRate = 48000.0f;
    float gain = 0.35f;

    RcHighPass inputC2;
    RcHighPass gainPathC3;
    RcHighPass outputC4;
    RcHighPass outputCoupling;
    RcLowPass lm741FeedbackCap;
    RcLowPass postClipC6;
    RcLowPass outputLoad;
    DcBlock opDc;
    DcBlock outDc;
    rbshared::OpAmpStage lm741;
    rbcomponents::AsymDiodeStringClipper outputDiodes;

    void updateComponentValues()
    {
        const float g = clamp01(gain);
        inputC2.setRC(sampleRate, kR5 + kR6, kC2);
        gainPathC3.setRC(sampleRate, kR7 + (1.0f - g) * kGainPot, kC3);
        outputC4.setRC(sampleRate, kR9 + kVolumePot, kC4);
        outputCoupling.setRC(sampleRate, kVolumePot, kC4);
        lm741FeedbackCap.setRC(sampleRate, kR8, kC5);
        postClipC6.setRC(sampleRate, kR9, kC6);
        outputLoad.setHz(sampleRate, 14500.0f);
        outputDiodes.setSpec(rbcomponents::diode1N4148());
        outputDiodes.setSeries(1, 2);
        outputDiodes.setSourceR(1800.0f - 650.0f * g);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        lm741.setSpec(rbshared::lm741Spec());
        lm741.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC2.reset();
        gainPathC3.reset();
        outputC4.reset();
        outputCoupling.reset();
        lm741FeedbackCap.reset();
        postClipC6.reset();
        outputLoad.reset();
        opDc.reset();
        outDc.reset();
        lm741.reset();
        outputDiodes.reset();
        updateComponentValues();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float g = clamp01(gain);
        const float g2 = g * g;

        float x = inputC2.process(0.97f * in);
        const float shaped = gainPathC3.process(x);

        // LM741 gain stage. The 22 pF cap and old op-amp slew behavior make the
        // high end rounder as gain rises.
        const float feedback = lm741FeedbackCap.process(shaped);
        float y = (shaped - 0.20f * feedback) * (2.4f + 16.0f * g + 42.0f * g2);
        y = lm741.process(y, 2.0f + 20.0f * g);
        y = std::tanh(0.82f * y) + 0.18f * std::tanh(0.22f * y);
        y = opDc.process(y);
        y = outputC4.process(y);

        y = outputDiodes.process(1.85f * y);
        y = postClipC6.process(y);
        y = outputLoad.process(outDc.process(y));
        y = outputCoupling.process(y);

        return std::tanh(y * (0.78f / (1.0f + 0.16f * g)));
    }
};

} // namespace vintagedistortion

#endif // VINTAGE_DISTORTION_CORE_H
