#ifndef STANDARD_DISTORTION_CORE_H
#define STANDARD_DISTORTION_CORE_H

/*
 * First-edition Boss DS-1 core from pedals/standard distortion.pdf.
 * Runs at the wrapper's 4x sample rate.
 *
 * Audio path:
 * C1/Q1 emitter follower -> Q6 switch -> C3/Q2 common-emitter preamp ->
 * C5/TA7136P gain stage -> R14/C9 -> D4/D5 1S2473 shunt clipper ->
 * C11/R16/R17/C12/VR3 tone network -> VR2 level -> Q7 switch -> Q3 buffer.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace standarddistortion {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float onePoleCoef(float hz, float sampleRate)
{
    const float nyquist = 0.45f * sampleRate;
    const float fc = hz < 1.0f ? 1.0f : (hz > nyquist ? nyquist : hz);
    return 1.0f - std::exp(-2.0f * kPi * fc / sampleRate);
}

class RcLowPass
{
    float coefficient = 0.0f;
    float state = 0.0f;

public:
    void setRC(float sampleRate, float resistance, float capacitance)
    {
        setFrequency(sampleRate, 1.0f / (2.0f * kPi * resistance * capacitance));
    }

    void setFrequency(float sampleRate, float hz)
    {
        coefficient = onePoleCoef(hz, sampleRate);
    }

    void reset() { state = 0.0f; }

    float process(float x)
    {
        state += coefficient * (x - state);
        if (std::fabs(state) < 1.0e-15f)
            state = 0.0f;
        return state;
    }
};

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
        y1 = std::fabs(y) < 1.0e-15f ? 0.0f : y;
        return y1;
    }
};

class Ta7136OutputStage
{
    const rbshared::OpAmpSpec spec = rbshared::ta7136apSpec();
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
        const float rail = 3.1f / spec.voltsPerUnit;
        const float desired = rail * std::tanh(target / rail);

        float next = state + coefficient * (desired - state);
        const float maxStep = (spec.slewVPerUs * 1000000.0f / sampleRate)
                            / spec.voltsPerUnit;
        const float delta = next - state;
        if (delta > maxStep)
            next = state + maxStep;
        else if (delta < -maxStep)
            next = state - maxStep;
        state = std::fabs(next) < 1.0e-15f ? 0.0f : next;
        return state;
    }
};

class StandardDistortionCore
{
    float sampleRate = 192000.0f;
    float dist = 0.45f;
    float tone = 0.50f;
    float level = 0.62f;
    float icGain = 1.0f;
    float outputGain = 0.0f;

    RcHighPass inputC1;
    RcHighPass q1OutputC2;
    RcHighPass q6OutputC3;
    RcHighPass q2OutputC5;
    RcHighPass icOutputC9;
    RcHighPass toneHighPass;
    RcHighPass toneOutputC13;
    RcHighPass finalOutputC14;
    RcLowPass q1Bandwidth;
    RcLowPass q2CollectorFeedback;
    RcLowPass icFeedbackCompensation;
    RcLowPass clipperCapC10;
    RcLowPass toneLowPass;
    RcLowPass toneCeiling;
    RcLowPass q3Bandwidth;
    Ta7136OutputStage ta7136;
    rbcomponents::AntiParallelDiodePair clipper;

    static float q2CommonEmitter(float x)
    {
        // Q2 (2SC2240/2SC3378) is the only transistor used as a voltage-gain
        // stage. This incremental collector curve retains its inversion and
        // slightly unequal headroom without turning Q1/Q3 into extra clippers.
        const float drive = 2.35f * x;
        const float positive = 1.34f * (1.0f - std::exp(-0.82f * std::fabs(drive)));
        const float negative = 1.22f * (1.0f - std::exp(-0.94f * std::fabs(drive)));
        return drive >= 0.0f ? -positive : negative;
    }

    void updateControls()
    {
        const float d = clamp01(dist); // VR1 100kB, linear
        const float t = clamp01(tone); // VR3 20kB, linear

        // VR1 and R13=4.7k form the gain leg. The 27k IC bias/feed network and
        // C7/C24 reduce closed-loop bandwidth as the pot resistance rises.
        icGain = 1.0f + (100000.0f * d) / 4700.0f;
        const float feedbackPole = 1.0f /
            (2.0f * kPi * (27000.0f + 100000.0f * d) * 250.0e-12f);
        icFeedbackCompensation.setFrequency(sampleRate, feedbackPole);

        // The passive tone branches are loaded by opposite halves of VR3.
        const float toneLowCap = 47.0e-9f + 42.0e-9f * t;
        toneLowPass.setRC(sampleRate, 6800.0f + 20000.0f * t, toneLowCap);
        const float inverseTone = 1.0f - t;
        const float highResistance = 3600.0f
            + 29600.0f * inverseTone - 6400.0f * inverseTone * inverseTone;
        toneHighPass.setRC(sampleRate, highResistance, 22.0e-9f);
        const float d4 = d * d * d * d;
        const float diodeLoading = 25000.0f * d4 * 4.0f * t * (1.0f - t)
                                 + 5000.0f * d4 * (1.0f - t) * (1.0f - t);
        toneCeiling.setFrequency(sampleRate,
                                 2200.0f + 17800.0f * t * t + diodeLoading);

        // VR2 is 100kB. The fixed factor represents the passive loss through
        // the tone network and the near-unity Q3 emitter follower recovery.
        outputGain = 2.0f * clamp01(level);
    }

public:
    StandardDistortionCore()
    {
        clipper.setSpec(rbcomponents::diode1S2473());
        clipper.setSourceR(2200.0f); // R14
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        ta7136.setSampleRate(sampleRate);

        inputC1.setRC(sampleRate, 470000.0f, 47.0e-9f);
        q1OutputC2.setRC(sampleRate, 1000000.0f, 470.0e-9f);
        q6OutputC3.setRC(sampleRate, 100000.0f, 47.0e-9f);
        q2OutputC5.setRC(sampleRate, 100000.0f, 470.0e-9f);
        icOutputC9.setRC(sampleRate, 15000.0f, 470.0e-9f);
        toneOutputC13.setRC(sampleRate, 1000000.0f, 47.0e-9f);
        finalOutputC14.setRC(sampleRate, 100000.0f, 1.0e-6f);

        q1Bandwidth.setFrequency(sampleRate, 32000.0f);
        q2CollectorFeedback.setRC(sampleRate, 470000.0f, 250.0e-12f);
        clipperCapC10.setRC(sampleRate, 2200.0f, 10.0e-9f);
        q3Bandwidth.setFrequency(sampleRate, 42000.0f);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q1OutputC2.reset();
        q6OutputC3.reset();
        q2OutputC5.reset();
        icOutputC9.reset();
        toneHighPass.reset();
        toneOutputC13.reset();
        finalOutputC14.reset();
        q1Bandwidth.reset();
        q2CollectorFeedback.reset();
        icFeedbackCompensation.reset();
        clipperCapC10.reset();
        toneLowPass.reset();
        toneCeiling.reset();
        q3Bandwidth.reset();
        ta7136.reset();
        clipper.reset();
        updateControls();
    }

    void setParams(float newDist, float newTone, float newLevel)
    {
        dist = clamp01(newDist);
        tone = clamp01(newTone);
        level = clamp01(newLevel);
        updateControls();
    }

    float process(float input)
    {
        // Q1 emitter follower and Q6 on-state electronic switch.
        float x = inputC1.process(input);
        x = q1Bandwidth.process(0.992f * x);
        x = q1OutputC2.process(x);
        x = q6OutputC3.process(0.995f * x);

        // Q2 common-emitter preamp with R7/C4 collector-to-base feedback.
        x = q2CommonEmitter(x);
        x = q2CollectorFeedback.process(x);
        x = q2OutputC5.process(x);

        // TA7136P gain stage. Rail limiting happens before its bandwidth and
        // slew response, as it does in the physical IC.
        float y = ta7136.process(icFeedbackCompensation.process(icGain * x), icGain);
        y = icOutputC9.process(y);

        // D4/D5 are the only hard clipping pair in the audible path. C10 is
        // the real 10 nF shunt capacitor at the diode node.
        y = clipper.process(y * 3.0f) / 3.0f;
        y = clipperCapC10.process(y);

        // VR3 blends the low-pass and high-pass branches. At either end the
        // opposite branch remains loaded by the 20k track, as in the circuit.
        const float low = toneLowPass.process(y);
        const float high = toneHighPass.process(y);
        const float t = clamp01(tone);
        const float d = clamp01(dist);
        const float d4 = d * d * d * d;
        const float lowWeight = 0.70f + 0.30f * t - t * t;
        // The clipped node's dynamic resistance falls at high Dist settings,
        // coupling more upper-mid content into the passive tone network. This
        // is strongest near the centre of VR3 and still audible at the dark end.
        const float distortionBrightness = d4 *
            (0.15f * (1.0f - t) + 0.45f * 4.0f * t * (1.0f - t));
        const float highWeight = 0.30f - 0.26f * t + 1.12f * t * t
                               + distortionBrightness;
        const float centreLoading = 1.0f - 0.15f * 4.0f * t * (1.0f - t);
        const float toneLoadGain = 0.84f + 0.19f * t + 0.26f * t * t;
        const float brightnessLoading = 0.80f
            + 0.87f * 4.0f * t * (1.0f - t);
        y = toneCeiling.process(1.48f * toneLoadGain * centreLoading
                              / (1.0f + brightnessLoading * distortionBrightness)
                              * (lowWeight * low + highWeight * high));

        // VR2 Level, Q7 on-state switch and Q3 emitter follower.
        y *= outputGain;
        y = toneOutputC13.process(y);
        y = q3Bandwidth.process(0.985f * y);
        return finalOutputC14.process(y);
    }
};

} // namespace standarddistortion

#endif // STANDARD_DISTORTION_CORE_H
