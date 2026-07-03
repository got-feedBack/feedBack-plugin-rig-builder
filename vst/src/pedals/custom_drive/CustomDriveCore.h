#ifndef CUSTOM_DRIVE_CORE_H
#define CUSTOM_DRIVE_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace customdrive {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.70f);
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

class CustomDriveCore
{
    // Local custom-drive/OCD-style schematic:
    // C1 22 nF input, X2 drive in first op-amp feedback, MOSFET+diode clipper,
    // second op-amp recovery, HP/LP switch around 22 k/33 k + C11 47 nF,
    // X2 Drive A1M, X4 Tone B10K, X5 Volume A100K from pedals/OCD.pdf.
    static constexpr float kC1 = 22.0e-9f;
    static constexpr float kR2 = 1000000.0f;
    static constexpr float kR3 = 10000.0f;
    static constexpr float kR6 = 470000.0f;
    static constexpr float kR5 = 2200.0f;
    static constexpr float kC4 = 100.0e-9f;
    static constexpr float kR8 = 18000.0f;
    static constexpr float kDrivePot = 1000000.0f;
    static constexpr float kC6 = 220.0e-12f;
    static constexpr float kR9 = 10000.0f;
    static constexpr float kC7 = 10.0e-9f;
    static constexpr float kR10 = 10000.0f;
    static constexpr float kR12 = 220000.0f;
    static constexpr float kR11 = 39000.0f;
    static constexpr float kC8 = 100.0e-9f;
    static constexpr float kR13 = 150000.0f;
    static constexpr float kC9 = 220.0e-12f;
    static constexpr float kC10 = 10.0e-6f;
    static constexpr float kR14 = 22000.0f;
    static constexpr float kR15 = 33000.0f;
    static constexpr float kC11 = 47.0e-9f;
    static constexpr float kTonePot = 10000.0f;
    static constexpr float kVolumePot = 100000.0f;

    float sampleRate = 48000.0f;
    float drive = 0.22f;
    float tone = 0.50f;
    float voice = 0.0f;

    RcHighPass inputC1;
    RcHighPass driveGroundC4;
    RcHighPass clipToSecond;
    RcHighPass secondGroundC8;
    RcHighPass outputC10;
    RcLowPass op1FeedbackC6;
    RcLowPass mosfetStrayC7;
    RcLowPass op2FeedbackC9;
    RcLowPass toneLow;
    RcLowPass toneHighBase;
    RcLowPass outputLoad;
    DcBlock op1Dc;
    DcBlock clipDc;
    DcBlock op2Dc;
    rbshared::OpAmpStage op1;
    rbshared::OpAmpStage op2;
    rbcomponents::OcdMosfetGeClipper mosfetGeClipper;

    void updateComponentValues()
    {
        const float d = audioTaper(drive);
        const float t = clamp01(tone);
        const float hp = voice >= 0.5f ? 1.0f : 0.0f;

        inputC1.setRC(sampleRate, kR2 + kR3, kC1);
        driveGroundC4.setRC(sampleRate, kR5 + (1.0f - d) * kDrivePot, kC4);
        clipToSecond.setRC(sampleRate, kR10 + kR12, kC7);
        secondGroundC8.setRC(sampleRate, kR11, kC8);
        outputC10.setRC(sampleRate, kVolumePot, kC10);

        op1FeedbackC6.setRC(sampleRate, kR8 + 0.30f * kDrivePot * d, kC6);
        mosfetStrayC7.setHz(sampleRate, 8200.0f - 1800.0f * d);
        op2FeedbackC9.setRC(sampleRate, kR13, kC9);

        const float switchR = hp > 0.5f ? kR14 : kR15;
        toneLow.setRC(sampleRate, switchR + (1.0f - t) * kTonePot, kC11);
        toneHighBase.setRC(sampleRate, switchR + t * kTonePot, kC11);
        outputLoad.setHz(sampleRate, 12500.0f + 1600.0f * hp);
        mosfetGeClipper.setHardness(0.35f + 0.65f * d);
        mosfetGeClipper.setSourceR(kR9 + 2200.0f + 1800.0f * (1.0f - d));
    }

    float toneNetwork(float x)
    {
        const float t = clamp01(tone);
        const float hp = voice >= 0.5f ? 1.0f : 0.0f;
        const float low = toneLow.process(x);
        const float highBase = toneHighBase.process(x);
        const float high = x - 0.75f * highBase;
        return low * (1.04f - 0.70f * t)
             + high * (0.22f + 1.04f * t + 0.16f * hp)
             + x * (0.08f + 0.04f * hp);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        op1.setSpec(rbshared::tl082Spec());
        op2.setSpec(rbshared::tl082Spec());
        op1.setSampleRate(sampleRate);
        op2.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        driveGroundC4.reset();
        clipToSecond.reset();
        secondGroundC8.reset();
        outputC10.reset();
        op1FeedbackC6.reset();
        mosfetStrayC7.reset();
        op2FeedbackC9.reset();
        toneLow.reset();
        toneHighBase.reset();
        outputLoad.reset();
        op1Dc.reset();
        clipDc.reset();
        op2Dc.reset();
        op1.reset();
        op2.reset();
        mosfetGeClipper.reset();
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

    void setVoice(float v)
    {
        voice = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = audioTaper(drive);
        const float hp = voice >= 0.5f ? 1.0f : 0.0f;

        float x = inputC1.process(0.97f * in);
        const float groundLeg = driveGroundC4.process(x);
        const float fb = op1FeedbackC6.process(x);
        float y = (x + 0.78f * groundLeg - 0.16f * fb) * (2.0f + 16.0f * d + 28.0f * d * d);
        y = op1.process(y, 2.0f + 18.0f * d);
        y = std::tanh(0.78f * y) + 0.22f * std::tanh(0.24f * y);
        y = op1Dc.process(y);

        y = mosfetStrayC7.process(y);
        y = 0.56f * mosfetGeClipper.process((2.45f + 0.35f * hp) * y);
        y = clipDc.process(y);
        y = clipToSecond.process(y);

        const float op2Fb = op2FeedbackC9.process(y);
        const float op2Ground = secondGroundC8.process(y);
        y = (y + 0.36f * op2Ground - 0.16f * op2Fb) * (1.8f + 1.0f * hp);
        y = op2.process(y, 2.0f + 3.0f * hp);
        y = std::tanh(0.92f * y);
        y = op2Dc.process(y);

        y = outputC10.process(y);
        y = toneNetwork(y);
        y = outputLoad.process(y);

        return std::tanh(y * (0.68f / (1.0f + 0.18f * d)));
    }
};

} // namespace customdrive

#endif // CUSTOM_DRIVE_CORE_H
