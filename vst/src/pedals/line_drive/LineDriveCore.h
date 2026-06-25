#ifndef LINE_DRIVE_CORE_H
#define LINE_DRIVE_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace linedrive {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
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
        const float y = x - x1 + 0.9985f * y1;
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class LineDriveCore
{
    // Boss OS-2 schematic anchors from pedals/line drive.png:
    // C22/R28/R33 input bias network, dual VR3 270 k Drive sections, D3/D4
    // distortion clipper, asymmetric overdrive feedback diodes, VR4 20 k Color,
    // VR2 20 k Tone and VR1 50 k Level. This core keeps those stages separate
    // so Color is a real blend, not a post-EQ shortcut.
    static constexpr float kC22 = 47.0e-9f;
    static constexpr float kR28 = 10000.0f;
    static constexpr float kR33 = 1000000.0f;
    static constexpr float kDrivePot = 270000.0f;
    static constexpr float kTonePot = 20000.0f;
    static constexpr float kColorPot = 20000.0f;
    static constexpr float kLevelPot = 50000.0f;
    static constexpr float kSmallFeedbackCap = 100.0e-12f;
    static constexpr float kToneCap = 22.0e-9f;
    static constexpr float kCouplingCap = 47.0e-9f;

    float sampleRate = 48000.0f;
    float drive = 0.45f;
    float tone = 0.50f;
    float color = 0.58f;

    RcHighPass inputC22;
    RcHighPass odDriveLeg;
    RcHighPass distDriveLeg;
    RcHighPass blendCoupling;
    RcHighPass outputCoupling;
    RcLowPass inputBufferPole;
    RcLowPass odFeedbackCap;
    RcLowPass odDiodeCap;
    RcLowPass distFeedbackCap;
    RcLowPass distDiodeCap;
    RcHighPass colorOdCoupling;
    RcHighPass colorDistCoupling;
    RcLowPass toneLowLeg;
    RcLowPass toneHighBleed;
    RcLowPass outputLoad;
    DcBlock odDc;
    DcBlock distDc;
    DcBlock mixDc;
    rbshared::OpAmpStage odOpamp;
    rbshared::OpAmpStage distOpamp;
    rbcomponents::AsymDiodeStringClipper odFeedbackDiodes;
    rbcomponents::AntiParallelDiodePair distHardClipper;

    void updateComponentValues()
    {
        const float d = smoothstep(drive);
        const float t = clamp01(tone);
        const float c = clamp01(color);

        inputC22.setRC(sampleRate, kR28 + kR33, kC22);
        inputBufferPole.setHz(sampleRate, 14500.0f);

        odDriveLeg.setRC(sampleRate, 10000.0f + (1.0f - d) * 0.35f * kDrivePot, 47.0e-9f);
        distDriveLeg.setRC(sampleRate, 22000.0f + (1.0f - d) * 0.30f * kDrivePot, 100.0e-9f);
        odFeedbackCap.setRC(sampleRate, 68000.0f + d * kDrivePot, kSmallFeedbackCap);
        distFeedbackCap.setRC(sampleRate, 33000.0f + 0.45f * d * kDrivePot, kSmallFeedbackCap);
        odDiodeCap.setHz(sampleRate, 7200.0f - 1600.0f * d + 700.0f * t);
        distDiodeCap.setHz(sampleRate, 6800.0f - 1900.0f * d + 1500.0f * t);

        colorOdCoupling.setRC(sampleRate, 33000.0f + c * kColorPot, 1.0e-6f);
        colorDistCoupling.setRC(sampleRate, 12000.0f + (1.0f - c) * kColorPot, 1.0e-6f);
        blendCoupling.setRC(sampleRate, 22000.0f + 0.5f * kColorPot, kCouplingCap);

        toneLowLeg.setRC(sampleRate, 6800.0f + (1.0f - t) * kTonePot, kToneCap);
        toneHighBleed.setRC(sampleRate, 4700.0f + t * kTonePot, 4.7e-9f);
        outputCoupling.setRC(sampleRate, kLevelPot, 1.0e-6f);
        outputLoad.setHz(sampleRate, 11000.0f + 3000.0f * t - 1400.0f * c);

        odFeedbackDiodes.setSpec(rbcomponents::diode1S2473());
        odFeedbackDiodes.setSeries(1, 2);
        odFeedbackDiodes.setSourceR(2200.0f - 900.0f * d);
        distHardClipper.setSpec(rbcomponents::diode1S2473());
        distHardClipper.setSourceR(2300.0f - 950.0f * d);
    }

    float toneNetwork(float x)
    {
        const float t = clamp01(tone);
        const float low = toneLowLeg.process(x);
        const float bleed = toneHighBleed.process(x);
        const float high = x - 0.82f * bleed;
        return low * (0.92f - 0.52f * t)
             + high * (0.26f + 1.02f * t)
             + x * 0.06f;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        odOpamp.setSpec(rbshared::m5218Spec());
        distOpamp.setSpec(rbshared::m5218Spec());
        odOpamp.setSampleRate(sampleRate);
        distOpamp.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC22.reset();
        odDriveLeg.reset();
        distDriveLeg.reset();
        blendCoupling.reset();
        outputCoupling.reset();
        inputBufferPole.reset();
        odFeedbackCap.reset();
        odDiodeCap.reset();
        distFeedbackCap.reset();
        distDiodeCap.reset();
        colorOdCoupling.reset();
        colorDistCoupling.reset();
        toneLowLeg.reset();
        toneHighBleed.reset();
        outputLoad.reset();
        odDc.reset();
        distDc.reset();
        mixDc.reset();
        odOpamp.reset();
        distOpamp.reset();
        odFeedbackDiodes.reset();
        distHardClipper.reset();
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

    void setColor(float v)
    {
        color = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = smoothstep(drive);
        const float c = clamp01(color);

        float x = inputC22.process(0.96f * in);
        x = inputBufferPole.process(x);
        x = std::tanh(1.04f * x);

        float od = odDriveLeg.process(x);
        const float odFb = odFeedbackCap.process(od);
        od = (od - 0.18f * odFb) * (1.25f + 9.0f * drive + 15.0f * d);
        od = odOpamp.process(od, 2.0f + 15.0f * d);
        od = odFeedbackDiodes.process(od + 0.010f * drive);
        od = 0.92f * od + 0.08f * std::tanh(0.34f * od);
        od = odDiodeCap.process(od);
        od = odDc.process(od);

        float dist = distDriveLeg.process(x);
        const float distFb = distFeedbackCap.process(dist);
        dist = (dist - 0.12f * distFb) * (2.2f + 14.0f * drive + 28.0f * d);
        dist = distOpamp.process(dist, 4.0f + 28.0f * d);
        dist = distHardClipper.process(2.55f * dist);
        dist = 0.74f * dist + 0.26f * std::tanh(2.1f * dist);
        dist = distDiodeCap.process(dist);
        dist = distDc.process(dist);

        od = colorOdCoupling.process(od);
        dist = colorDistCoupling.process(dist);
        const float odMix = std::cos(0.5f * kPi * c);
        const float distMix = std::sin(0.5f * kPi * c);
        float y = od * odMix + dist * distMix;
        y = blendCoupling.process(y);

        const float cleanLeak = 0.08f * (1.0f - d) * (1.0f - c);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;
        y = mixDc.process(y);
        y = toneNetwork(y);
        y = outputLoad.process(outputCoupling.process(y));

        const float trim = 1.02f / (1.0f + 0.14f * drive + 0.12f * d + 0.10f * c);
        return std::tanh(1.08f * y * trim);
    }
};

} // namespace linedrive

#endif // LINE_DRIVE_CORE_H
