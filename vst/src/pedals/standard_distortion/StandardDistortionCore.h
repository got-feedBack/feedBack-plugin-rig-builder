#ifndef STANDARD_DISTORTION_CORE_H
#define STANDARD_DISTORTION_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace standarddistortion {

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

class StandardDistortionCore
{
    // Boss DS-1 first-edition schematic:
    // C1 47 nF input, TA7136P gain stage, DIST 100 k, D4/D5 hard clip,
    // passive TONE network, LEVEL 100 k, FET output buffer.
    static constexpr float kC1 = 47.0e-9f;
    static constexpr float kR1 = 1000.0f;
    static constexpr float kR2 = 470000.0f;
    static constexpr float kC3 = 47.0e-9f;
    static constexpr float kC4 = 250.0e-12f;
    static constexpr float kR7 = 47000.0f;
    static constexpr float kR8 = 10000.0f;
    static constexpr float kC6 = 150.0e-12f;
    static constexpr float kR11 = 100000.0f;
    static constexpr float kR12 = 27000.0f;
    static constexpr float kDistPot = 100000.0f;
    static constexpr float kR14 = 2200.0f;
    static constexpr float kC8 = 470.0e-9f;
    static constexpr float kC9 = 470.0e-9f;
    static constexpr float kC11 = 22.0e-9f;
    static constexpr float kR15 = 2200.0f;
    static constexpr float kR16 = 6800.0f;
    static constexpr float kTonePot = 20000.0f;
    static constexpr float kC12 = 100.0e-9f;
    static constexpr float kR17 = 6800.0f;
    static constexpr float kLevelPot = 100000.0f;
    static constexpr float kC13 = 47.0e-9f;

    float sampleRate = 48000.0f;
    float dist = 0.45f;
    float tone = 0.50f;

    RcHighPass inputC1;
    RcHighPass q1ToIc;
    RcHighPass distCoupling;
    RcHighPass clipToTone;
    RcHighPass toneToLevel;
    RcHighPass outputC13;
    RcLowPass q1Miller;
    RcLowPass icFeedbackC4;
    RcLowPass icFeedbackC6;
    RcLowPass clipInputShelf;
    RcLowPass toneDark;
    RcLowPass toneBrightBase;
    RcLowPass outputLoad;
    DcBlock q1Dc;
    DcBlock icDc;
    DcBlock outDc;
    rbshared::OpAmpStage ta7136;
    rbcomponents::AntiParallelDiodePair clipper;

    static inline float parallel(float a, float b)
    {
        return (a * b) / (a + b);
    }

    static inline float bjtCurve(float v, float posK, float negK, float posRail, float negRail)
    {
        if (v >= 0.0f)
            return posRail * (1.0f - std::exp(-posK * v));
        return -negRail * (1.0f - std::exp(negK * v));
    }

    static inline float bjtStage(float x, float drive, float bias, float posK,
                                 float negK, float posRail, float negRail)
    {
        const float idle = bjtCurve(bias, posK, negK, posRail, negRail);
        return bjtCurve(bias + drive * x, posK, negK, posRail, negRail) - idle;
    }

    void updateComponentValues()
    {
        const float d = clamp01(dist);
        const float t = clamp01(tone);

        inputC1.setRC(sampleRate, kR1 + kR2, kC1);
        q1ToIc.setRC(sampleRate, kR7 + kR8, kC3);
        distCoupling.setRC(sampleRate, kR14 + (1.0f - d) * kDistPot, kC8 + kC9);
        clipToTone.setRC(sampleRate, kR15 + kR16 + kTonePot, kC11);
        toneToLevel.setRC(sampleRate, kLevelPot, kC12);
        outputC13.setRC(sampleRate, 1000000.0f, kC13);

        q1Miller.setHz(sampleRate, 10500.0f);
        icFeedbackC4.setRC(sampleRate, kR7, kC4);
        icFeedbackC6.setRC(sampleRate, kR11 + kR12, kC6);
        clipInputShelf.setHz(sampleRate, 7200.0f - 2200.0f * d);
        toneDark.setRC(sampleRate, kR17 + (1.0f - t) * kTonePot, kC12);
        toneBrightBase.setRC(sampleRate, kR15 + t * kTonePot, kC11);
        outputLoad.setHz(sampleRate, 12000.0f);

        clipper.setSpec(rbcomponents::diode1S2473());
        clipper.setSourceR(2600.0f - 1200.0f * d);
    }

    float toneStack(float x)
    {
        const float t = clamp01(tone);
        const float low = toneDark.process(x);
        const float highBase = toneBrightBase.process(x);
        const float high = x - 0.78f * highBase;
        const float scoop = 0.16f + 0.22f * (1.0f - std::fabs(2.0f * t - 1.0f));

        return low * (1.14f - 0.82f * t)
             + high * (0.20f + 1.20f * t)
             - highBase * scoop;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        ta7136.setSpec(rbshared::ta7136apSpec());
        ta7136.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q1ToIc.reset();
        distCoupling.reset();
        clipToTone.reset();
        toneToLevel.reset();
        outputC13.reset();
        q1Miller.reset();
        icFeedbackC4.reset();
        icFeedbackC6.reset();
        clipInputShelf.reset();
        toneDark.reset();
        toneBrightBase.reset();
        outputLoad.reset();
        q1Dc.reset();
        icDc.reset();
        outDc.reset();
        ta7136.reset();
        clipper.reset();
        updateComponentValues();
    }

    void setDist(float v)
    {
        dist = clamp01(v);
        updateComponentValues();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = clamp01(dist);
        const float d2 = d * d;

        float x = inputC1.process(0.965f * in);

        // Q1 transistor buffer/preamp into the TA7136 input.
        x = bjtStage(x, 2.4f + 1.4f * d, -0.014f,
                     1.16f, 1.04f, 1.10f, 0.92f);
        x = q1Miller.process(q1Dc.process(x));
        x = q1ToIc.process(x);

        // TA7136P high-gain stage. The small feedback caps shave the top end as
        // the 100 k DIST pot drives harder.
        const float fb1 = icFeedbackC4.process(x);
        const float fb2 = icFeedbackC6.process(x);
        float y = bjtStage(x - 0.20f * fb1 - 0.14f * fb2,
                           6.8f + 30.0f * d + 42.0f * d2,
                           -0.030f, 1.45f + 0.30f * d, 1.22f + 0.20f * d,
                           1.35f, 1.18f);
        y = ta7136.process(y, 8.0f + 38.0f * d);
        y = icDc.process(y);
        y = distCoupling.process(y);
        y = clipInputShelf.process(y);

        // D4/D5 silicon hard clip after the IC. Keep a little IC hair in
        // parallel so DIST low settings do not become clean bypass.
        const float clipped = clipper.process(2.0f * y);
        y = 0.90f * clipped + 0.10f * std::tanh(0.45f * y);

        y = clipToTone.process(y);
        y = toneStack(y);
        y = toneToLevel.process(y);

        // Q7 output FET buffer after the LEVEL pot.
        y = std::tanh(1.15f * y);
        y = outputLoad.process(outDc.process(y));
        y = outputC13.process(y);

        return std::tanh(y * (0.70f / (1.0f + 0.20f * d)));
    }
};

} // namespace standarddistortion

#endif // STANDARD_DISTORTION_CORE_H
