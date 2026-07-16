#ifndef ALLOY_DISTORTION_CORE_H
#define ALLOY_DISTORTION_CORE_H

/*
 * Boss HM-2 core derived from pedals/alloy distortion.pdf.
 * Runs at the wrapper's 4x sample rate.
 *
 * Audible path:
 * C1/Q1 buffer -> Q4 switch -> C6/Q6 NPN gain -> C11/Q7 PNP driver ->
 * IC1b with D3 versus D4+D5 asymmetric feedback clipping -> C12/D6-D7
 * series germanium crossover -> R30/D8-D9 hard clip -> IC1a buffer ->
 * VR4 Dist/IC3a and the fixed-frequency Color L/H board -> VR1 Level ->
 * Q10 switch -> Q3 output buffer.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace alloydistortion {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float denormal(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float audioTaper(float v)
{
    return (std::pow(10.0f, 2.0f * clamp01(v)) - 1.0f) / 99.0f;
}

static inline float onePoleCoef(float hz, float sampleRate)
{
    const float nyquist = 0.45f * sampleRate;
    const float fc = hz < 1.0f ? 1.0f : (hz > nyquist ? nyquist : hz);
    return 1.0f - std::exp(-2.0f * kPi * fc / sampleRate);
}

class RcHighPass
{
    float coefficient = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sampleRate, float resistance, float capacitance)
    {
        const float dt = 1.0f / sampleRate;
        const float rc = resistance * capacitance;
        coefficient = rc / (rc + dt);
    }

    void reset() { x1 = y1 = 0.0f; }

    float process(float x)
    {
        const float y = coefficient * (y1 + x - x1);
        x1 = x;
        y1 = denormal(y);
        return y1;
    }
};

class RcLowPass
{
    float coefficient = 0.0f;
    float state = 0.0f;

public:
    void setRC(float sampleRate, float resistance, float capacitance)
    {
        setFrequency(sampleRate,
                     1.0f / (2.0f * kPi * resistance * capacitance));
    }

    void setFrequency(float sampleRate, float hz)
    {
        coefficient = onePoleCoef(hz, sampleRate);
    }

    void reset() { state = 0.0f; }

    float process(float x)
    {
        state += coefficient * (x - state);
        state = denormal(state);
        return state;
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

    void set(float nb0, float nb1, float nb2,
             float na0, float na1, float na2)
    {
        const float invA0 = 1.0f / (std::fabs(na0) < 1.0e-12f ? 1.0f : na0);
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset() { z1 = z2 = 0.0f; }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = denormal(b1 * x - a1 * y + z2);
        z2 = denormal(b2 * x - a2 * y);
        return y;
    }

    void setPeaking(float sampleRate, float hz, float q, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sampleRate;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sampleRate, float hz, float slope, float gainDb)
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sampleRate;
        const float c = std::cos(w0);
        const float si = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = si * 0.5f
            * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class M5218OutputStage
{
    const rbshared::OpAmpSpec spec = rbshared::m5218Spec();
    float sampleRate = 192000.0f;
    float state = 0.0f;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        reset();
    }

    void reset() { state = 0.0f; }

    float process(float target, float noiseGain)
    {
        const float gain = noiseGain < 1.0f ? 1.0f : noiseGain;
        const float coefficient = onePoleCoef(spec.gbwHz / gain, sampleRate);
        const float rail = spec.posSwingV / spec.voltsPerUnit;
        const float desired = rail * std::tanh(target / rail);

        float next = state + coefficient * (desired - state);
        const float maxStep = (spec.slewVPerUs * 1000000.0f / sampleRate)
                            / spec.voltsPerUnit;
        const float delta = next - state;
        if (delta > maxStep)
            next = state + maxStep;
        else if (delta < -maxStep)
            next = state - maxStep;
        state = denormal(next);
        return state;
    }
};

class AlloyDistortionCore
{
    float sampleRate = 192000.0f;
    float dist = 0.55f;
    float colorLow = 0.82f;
    float colorHigh = 0.72f;
    float level = 0.62f;
    float driveAmount = 0.0f;
    float outputGain = 0.0f;

    RcHighPass inputC1;
    RcHighPass q1OutputC4;
    RcHighPass q4OutputC6;
    RcHighPass q6OutputC11;
    RcHighPass ic1bOutputC12;
    RcHighPass colorLowC30;
    RcHighPass colorMidC27;
    RcHighPass colorHighC26;
    RcHighPass colorOutputC34;
    RcHighPass levelOutputC32;
    RcHighPass finalOutputC3;
    RcLowPass inputBufferPole;
    RcLowPass q6FeedbackC10;
    RcLowPass feedbackC9;
    RcLowPass hardClipC16;
    RcLowPass colorCeiling;
    RcLowPass outputBufferPole;
    Biquad lowColorEq;
    Biquad highColorEq;
    Biquad presenceEq;
    M5218OutputStage clipOpamp;
    M5218OutputStage clipBuffer;
    M5218OutputStage colorMixer;
    rbcomponents::AsymDiodeStringClipper feedbackClipper;
    rbcomponents::AntiParallelDiodePair hardClipper;

    static float q6CommonEmitter(float x, float drive)
    {
        // Q6 2SC2240-GR: high-gain NPN stage with R16/C10 feedback and the
        // bypassed 22R emitter leg. The unequal collector headroom is retained.
        const float v = (2.7f + 27.3f * drive) * x;
        if (v >= 0.0f)
            return -1.02f * (1.0f - std::exp(-0.96f * v));
        return 0.91f * (1.0f - std::exp(1.13f * v));
    }

    static float q7CommonEmitter(float x, float drive)
    {
        // Q7 2SA970-GR is the complementary driver into IC1b. It adds the
        // opposite asymmetry before the D3 versus D4/D5 feedback network.
        const float v = (1.5f + 9.1f * drive) * x;
        if (v >= 0.0f)
            return 0.94f * (1.0f - std::exp(-1.10f * v));
        return -1.06f * (1.0f - std::exp(0.93f * v));
    }

    static float seriesGermaniumPair(float x)
    {
        // D6/D7 are in series with the signal. OA90 is the documented project
        // substitute for 1S188FM. This smooth physical-voltage transfer keeps
        // the crossover/noise-gate character without a discontinuity or click.
        const float volts = 3.0f * x;
        const float magnitude = std::fabs(volts);
        const float knee = 0.165f;
        const float softness = 0.025f;
        const float over = magnitude - knee;
        const float base = 0.5f * (-knee
                         + std::sqrt(knee * knee + softness * softness));
        const float conduction = 0.5f * (over
                               + std::sqrt(over * over + softness * softness))
                               - base;
        const float passed = 0.035f * magnitude
                           + (conduction > 0.0f ? conduction : 0.0f);
        return volts < 0.0f ? -passed / 3.0f : passed / 3.0f;
    }

    static float q3EmitterFollower(float x)
    {
        // Q3 runs from the 9 V rail around the 4.5 V virtual ground. Its usable
        // output swing is finite, but this buffer remains linear at normal
        // Level settings and only rounds peaks that exceed physical headroom.
        const float positiveRail = 1.34f;
        const float negativeRail = 1.28f;
        return x >= 0.0f
            ? positiveRail * std::tanh(x / positiveRail)
            : -negativeRail * std::tanh((-x) / negativeRail);
    }

    void updateControls()
    {
        // VR4 is 250kD. Its effective loaded law is substantially gentler than
        // an unloaded reverse-log curve; the power fit preserves useful gain
        // travel through noon instead of collapsing it into the first quarter.
        // VR2/VR3 are 10kG and VR1 is 120kA. Color changes gain, not frequency.
        driveAmount = std::pow(clamp01(dist), 1.8f);
        const float low = clamp01(colorLow);
        const float high = clamp01(colorHigh);

        lowColorEq.setPeaking(sampleRate, 105.0f, 0.72f,
                              7.0f + low);
        highColorEq.setPeaking(sampleRate, 1050.0f, 0.75f,
                               -9.6f + 27.2f * high);
        presenceEq.setHighShelf(sampleRate, 2800.0f, 0.72f,
                                -10.0f + 6.0f * high);

        outputGain = 11.9f * audioTaper(level);
    }

    float processColorBoard(float x)
    {
        const float lowControl = clamp01(colorLow);
        const float highControl = clamp01(colorHigh);

        // IC3b, IC2a and IC2b use fixed C30/C27/C26 networks. Their outputs
        // meet at IC3a through the Color pots and R52/R63 mixer resistors.
        float low = lowColorEq.process(colorLowC30.process(x));
        float mid = highColorEq.process(colorMidC27.process(x));
        float high = presenceEq.process(colorHighC26.process(x));
        const float mixed = 0.33f * x
            + (0.35f + 0.45f * lowControl) * low
            + (0.12f + 0.78f * highControl) * mid
            + (0.01f + 0.03f * highControl) * high;
        const float mixerScale = 0.40f / (1.0f + 0.45f * highControl);
        return (0.58f / mixerScale) * colorMixer.process(mixerScale * mixed, 6.0f);
    }

public:
    AlloyDistortionCore()
    {
        feedbackClipper.setSpec(rbcomponents::diode1S2473());
        feedbackClipper.setSeries(1, 2); // D3 versus D4+D5
        feedbackClipper.setSourceR(68000.0f); // R25 / feedback-loop scale
        hardClipper.setSpec(rbcomponents::diode1S2473());
        hardClipper.setSourceR(10000.0f); // R30
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        clipOpamp.setSampleRate(sampleRate);
        clipBuffer.setSampleRate(sampleRate);
        colorMixer.setSampleRate(sampleRate);

        inputC1.setRC(sampleRate, 1000000.0f, 47.0e-9f);   // C1/R8
        q1OutputC4.setRC(sampleRate, 1000000.0f, 1.0e-6f); // C4/R11
        q4OutputC6.setRC(sampleRate, 100000.0f, 47.0e-9f); // C6/R21
        q6OutputC11.setRC(sampleRate, 22000.0f, 47.0e-9f); // C11/R22
        ic1bOutputC12.setRC(sampleRate, 10000.0f, 1.0e-6f);// C12/R23

        colorLowC30.setRC(sampleRate, 100000.0f, 68.0e-9f);
        colorMidC27.setRC(sampleRate, 82000.0f, 6.8e-9f);
        colorHighC26.setRC(sampleRate, 100000.0f, 4.7e-9f);
        colorOutputC34.setRC(sampleRate, 10000.0f, 1.0e-6f);
        levelOutputC32.setRC(sampleRate, 1000000.0f, 1.0e-6f);
        finalOutputC3.setRC(sampleRate, 100000.0f, 1.0e-6f);

        inputBufferPole.setFrequency(sampleRate, 18000.0f);
        q6FeedbackC10.setRC(sampleRate, 470000.0f, 100.0e-12f);
        feedbackC9.setRC(sampleRate, 220000.0f, 100.0e-12f);
        hardClipC16.setRC(sampleRate, 10000.0f, 1.0e-9f);
        colorCeiling.setFrequency(sampleRate, 5500.0f);
        outputBufferPole.setFrequency(sampleRate, 36000.0f);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q1OutputC4.reset();
        q4OutputC6.reset();
        q6OutputC11.reset();
        ic1bOutputC12.reset();
        colorLowC30.reset();
        colorMidC27.reset();
        colorHighC26.reset();
        colorOutputC34.reset();
        levelOutputC32.reset();
        finalOutputC3.reset();
        inputBufferPole.reset();
        q6FeedbackC10.reset();
        feedbackC9.reset();
        hardClipC16.reset();
        colorCeiling.reset();
        outputBufferPole.reset();
        lowColorEq.reset();
        highColorEq.reset();
        presenceEq.reset();
        clipOpamp.reset();
        clipBuffer.reset();
        colorMixer.reset();
        feedbackClipper.reset();
        hardClipper.reset();
        updateControls();
    }

    void setParams(float newDist, float newColorLow,
                   float newColorHigh, float newLevel)
    {
        dist = clamp01(newDist);
        colorLow = clamp01(newColorLow);
        colorHigh = clamp01(newColorHigh);
        level = clamp01(newLevel);
        updateControls();
    }

    float process(float input)
    {
        // Q1 source follower and Q4 electronic switch stay essentially linear.
        float x = inputC1.process(input);
        x = inputBufferPole.process(0.992f * x);
        x = q1OutputC4.process(x);
        x = q4OutputC6.process(0.995f * x);

        // Q6 and Q7 are the real discrete voltage-gain stages before IC1b.
        float y = q6CommonEmitter(x, driveAmount);
        y = q6FeedbackC10.process(y);
        y = q6OutputC11.process(y);
        y = q7CommonEmitter(y, driveAmount);

        // IC1b and its asymmetric feedback network: one 1S2473 on the positive
        // branch and two in series on the negative branch.
        const float opampGain = 2.2f + 8.0f * driveAmount;
        y = clipOpamp.process(feedbackC9.process(opampGain * y), opampGain);
        y = feedbackClipper.process(3.0f * y) / 3.0f;
        y = ic1bOutputC12.process(y);

        // D6/D7 create crossover distortion in series; D8/D9 then hard-clip
        // the node after fixed R30. IC1a is the following unity buffer.
        y = seriesGermaniumPair(y);
        y = hardClipper.process(3.0f * y) / 3.0f;
        y = hardClipC16.process(y);
        y = clipBuffer.process(y, 1.0f);

        y = colorCeiling.process(processColorBoard(y));
        y = colorOutputC34.process(y);

        // VR1 Level, Q10 switch and Q3 emitter follower. There is no generic
        // post-DSP limiter; only the physical M5218 rail and diode stages clip.
        y *= outputGain;
        y = levelOutputC32.process(y);
        y = outputBufferPole.process(q3EmitterFollower(0.985f * y));
        return finalOutputC3.process(y);
    }
};

} // namespace alloydistortion

#endif // ALLOY_DISTORTION_CORE_H
