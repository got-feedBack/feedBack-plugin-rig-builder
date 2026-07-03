#ifndef SUPER_DRIVE_CORE_H
#define SUPER_DRIVE_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace superdrive {

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

class SuperDriveCore
{
    // Boss SD-1 schematic from pedals/super drive.pdf:
    // C1 47 nF input, uPC4558C feedback clipper, DRIVE 1 M,
    // asymmetric D4/D5/D6 silicon clipping, TONE 20 kW, LEVEL 100 kB.
    static constexpr float kC1 = 47.0e-9f;
    static constexpr float kR1 = 10000.0f;
    static constexpr float kR2 = 470000.0f;
    static constexpr float kC2 = 18.0e-9f;
    static constexpr float kR4 = 100000.0f;
    static constexpr float kR5 = 33000.0f;
    static constexpr float kDrivePot = 1000000.0f;
    static constexpr float kR6 = 4700.0f;
    static constexpr float kC3 = 47.0e-9f;
    static constexpr float kR7 = 10000.0f;
    static constexpr float kC4 = 18.0e-9f;
    static constexpr float kC5 = 27.0e-9f;
    static constexpr float kR8 = 470.0f;
    static constexpr float kC6 = 100.0e-9f;
    static constexpr float kR9 = 10000.0f;
    static constexpr float kTonePot = 20000.0f;
    static constexpr float kLevelPot = 100000.0f;
    static constexpr float kC8 = 47.0e-9f;

    float sampleRate = 48000.0f;
    float drive = 0.45f;
    float tone = 0.50f;

    RcHighPass inputC1;
    RcHighPass bufferToOpamp;
    RcHighPass feedbackC3;
    RcHighPass opampToTone;
    RcHighPass toneToLevel;
    RcHighPass outputC8;
    RcLowPass inputBufferMiller;
    RcLowPass opampRollOff;
    RcLowPass toneLow;
    RcLowPass toneHighBase;
    RcLowPass outputLoad;
    DcBlock q5Dc;
    DcBlock opDc;
    DcBlock outDc;
    rbshared::OpAmpStage upc4558;
    rbcomponents::AsymDiodeStringClipper feedbackDiodes;

    static inline float bjtCurve(float v, float posK, float negK, float posRail, float negRail)
    {
        if (v >= 0.0f)
            return posRail * (1.0f - std::exp(-posK * v));
        return -negRail * (1.0f - std::exp(negK * v));
    }

    static inline float bjtStage(float x, float amount)
    {
        const float idle = bjtCurve(-0.012f, 1.18f, 1.05f, 1.05f, 0.92f);
        return bjtCurve(-0.012f + amount * x, 1.18f, 1.05f, 1.05f, 0.92f) - idle;
    }

    void updateComponentValues()
    {
        const float d = clamp01(drive);
        const float t = clamp01(tone);
        inputC1.setRC(sampleRate, kR1 + kR2, kC1);
        bufferToOpamp.setRC(sampleRate, kR4, kC2);
        feedbackC3.setRC(sampleRate, kR6 + (1.0f - d) * kDrivePot, kC3);
        opampToTone.setRC(sampleRate, kR7 + kTonePot, kC4);
        toneToLevel.setRC(sampleRate, kLevelPot, kC6);
        outputC8.setRC(sampleRate, 1000000.0f, kC8);

        inputBufferMiller.setHz(sampleRate, 10500.0f);
        opampRollOff.setHz(sampleRate, 7600.0f - 2100.0f * d);
        toneLow.setRC(sampleRate, kR9 + (1.0f - t) * kTonePot, kC6);
        toneHighBase.setRC(sampleRate, kR8 + t * kTonePot, kC5);
        outputLoad.setHz(sampleRate, 11000.0f);

        feedbackDiodes.setSpec(rbcomponents::diode1S2473());
        feedbackDiodes.setSeries(1, 2);
        feedbackDiodes.setSourceR(2100.0f - 950.0f * d);
    }

    float toneStack(float x)
    {
        const float t = clamp01(tone);
        const float low = toneLow.process(x);
        const float highBase = toneHighBase.process(x);
        const float high = x - 0.78f * highBase;
        return low * (1.10f - 0.76f * t)
             + high * (0.18f + 1.10f * t)
             + highBase * 0.08f;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        upc4558.setSpec(rbshared::upc4558Spec());
        upc4558.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        bufferToOpamp.reset();
        feedbackC3.reset();
        opampToTone.reset();
        toneToLevel.reset();
        outputC8.reset();
        inputBufferMiller.reset();
        opampRollOff.reset();
        toneLow.reset();
        toneHighBase.reset();
        outputLoad.reset();
        q5Dc.reset();
        opDc.reset();
        outDc.reset();
        upc4558.reset();
        feedbackDiodes.reset();
        updateComponentValues();
    }

    void setDrive(float v)
    {
        drive = clamp01(v);
        updateComponentValues();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = clamp01(drive);
        const float d2 = d * d;

        float x = inputC1.process(0.965f * in);
        x = bjtStage(x, 2.1f + 1.2f * d);
        x = inputBufferMiller.process(q5Dc.process(x));
        x = bufferToOpamp.process(x);

        const float fbPath = feedbackC3.process(x);
        float y = (x + 0.65f * fbPath) * (1.6f + 10.0f * d + 28.0f * d2);
        y = opampRollOff.process(y);
        y = upc4558.process(y, 2.0f + 18.0f * d);
        y = feedbackDiodes.process(y + 0.015f * d);
        y = 0.92f * y + 0.08f * std::tanh(0.34f * y);
        y = opDc.process(y);

        y = opampToTone.process(y);
        y = toneStack(y);
        y = toneToLevel.process(y);
        y = outputLoad.process(outDc.process(std::tanh(1.12f * y)));
        y = outputC8.process(y);

        return std::tanh(y * (0.72f / (1.0f + 0.18f * d)));
    }
};

} // namespace superdrive

#endif // SUPER_DRIVE_CORE_H
