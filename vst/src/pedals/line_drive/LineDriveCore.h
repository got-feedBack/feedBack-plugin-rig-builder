#ifndef LINE_DRIVE_CORE_H
#define LINE_DRIVE_CORE_H

/*
 * Boss OS-2 OverDrive/Distortion core from pedals/line drive.png.
 * Runs at the wrapper's 4x sample rate.
 *
 * The dual 270k Drive pot feeds two complete parallel paths:
 *   OD:   IC2b with D7 versus D8+D9 asymmetric feedback clipping.
 *   DIST: IC2a multi-band gain -> C15/R23 -> D3/D4 shunt clipping -> Q2.
 * VR4 Color loads and blends those outputs before IC1a, then IC1b/VR2 form
 * the common Tone network, followed by VR1 Level and Q3 output buffer.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace linedrive {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float denormal(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
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
        const float positiveRail = 3.15f / spec.voltsPerUnit;
        const float negativeRail = 2.95f / spec.voltsPerUnit;
        const float desired = target >= 0.0f
            ? positiveRail * std::tanh(target / positiveRail)
            : -negativeRail * std::tanh((-target) / negativeRail);

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

class LineDriveCore
{
    float sampleRate = 192000.0f;
    float drive = 0.45f;
    float tone = 0.50f;
    float color = 0.58f;
    float level = 0.62f;
    float odFeedbackR = 1000.0f;
    float distFeedbackR = 12000.0f;
    float outputGain = 0.0f;

    RcHighPass inputC22;
    RcLowPass inputBufferPole;

    RcHighPass odInputC27;
    RcHighPass odGainLegC23;
    RcLowPass odFeedbackC26;
    RcHighPass odOutputC5;
    RcLowPass odMixC4;

    RcHighPass distInputC28;
    RcHighPass distGainLegC19;
    RcHighPass distTrebleLegC18;
    RcLowPass distFeedbackC17;
    RcHighPass distOutputC15;
    RcHighPass distSeriesC16;
    RcLowPass distBodyC20;
    RcHighPass distBufferC10;
    RcHighPass distBufferC9;
    RcLowPass distOutputC8;

    RcLowPass toneDark;
    RcHighPass tonePresence;
    RcLowPass toneCeiling;
    RcHighPass toneOutputC7;
    RcHighPass finalOutputC11;
    RcLowPass outputBufferPole;

    M5218OutputStage odOpamp;
    M5218OutputStage distOpamp;
    M5218OutputStage mixOpamp;
    M5218OutputStage toneOpamp;
    rbcomponents::AsymDiodeStringClipper odFeedbackDiodes;
    rbcomponents::AntiParallelDiodePair distHardClipper;

    void updateControls()
    {
        // The mechanically linked 270k tracks increase both feedback paths.
        // The loaded law keeps useful resolution around noon without changing
        // either branch's fixed capacitors or diode source resistances.
        const float track = std::pow(clamp01(drive), 1.45f);
        odFeedbackR = 1000.0f + 270000.0f * track;   // R37 + VR3a
        distFeedbackR = 12000.0f + 270000.0f * track;// R22 + VR3b

        odFeedbackDiodes.setSourceR(odFeedbackR);
        odFeedbackC26.setRC(sampleRate, odFeedbackR, 100.0e-12f);
        distFeedbackC17.setRC(sampleRate, distFeedbackR, 100.0e-12f);

        // VR1 is explicitly 50k linear in the schematic.
        outputGain = 2.55f * clamp01(level);
    }

    float processOverdrive(float x)
    {
        const float od = odInputC27.process(x);
        const float groundCurrentSignal = odGainLegC23.process(od);
        const float linearFeedback = (odFeedbackR / 100.0f)
                                   * groundCurrentSignal;
        const float compensated = odFeedbackC26.process(linearFeedback);

        // D7 versus D8+D9 sits across the feedback resistance. Solving the
        // Thevenin feedback voltage preserves the soft, asymmetric SD-1 side.
        const float diodeFeedback = odFeedbackDiodes.process(3.0f * compensated)
                                  / 3.0f;
        const float noiseGain = 1.0f + odFeedbackR / 100.0f;
        float y = odOpamp.process(od + diodeFeedback, noiseGain);
        y = odOutputC5.process(y);

        // R2/C4 is the fixed low-pass loading at the OD side of Color.
        return odMixC4.process(y);
    }

    float processDistortion(float x)
    {
        const float input = distInputC28.process(x);
        const float band60 = distGainLegC19.process(input);
        const float band3386 = distTrebleLegC18.process(input);

        // IC2a has two real frequency-dependent ground legs, analogous to the
        // RAT topology but with the OS-2 values shown in the schematic.
        float boost = (distFeedbackR / 1200.0f) * band60
                    + (distFeedbackR / 100.0f) * band3386;
        boost = distFeedbackC17.process(boost);
        const float noiseGain = 1.0f + distFeedbackR / 1200.0f;
        float y = distOpamp.process(input + boost, noiseGain);

        y = distOutputC15.process(y);
        y = distHardClipper.process(3.0f * y) / 3.0f;

        // C16/R24/R32 and the R27/C20/R26 return form the fixed post-clip
        // voicing. Keeping a body and a bright path models that bridged load.
        const float bright = distSeriesC16.process(y);
        const float body = distBodyC20.process(y);
        y = 0.64f * y + 0.32f * bright + 0.30f * body;
        y = distBufferC10.process(y);
        y = distBufferC9.process(0.985f * y); // Q2 emitter follower
        return distOutputC8.process(y);
    }

    float processTone(float x)
    {
        // IC1b's VR2/R1/C1 and R7/C6 network changes the balance around fixed
        // corner frequencies; Tone does not retune either capacitor.
        const float t = clamp01(tone);
        const float base = toneCeiling.process(x);
        const float dark = toneDark.process(base);
        const float presence = tonePresence.process(base);
        const float target = (1.10f - 0.35f * t) * dark
                           + (0.05f + 0.55f * t) * presence
                           + 0.20f * base;
        return toneOpamp.process(target, 3.1f);
    }

    static float q3EmitterFollower(float x)
    {
        const float positiveRail = 1.34f;
        const float negativeRail = 1.28f;
        return x >= 0.0f
            ? positiveRail * std::tanh(x / positiveRail)
            : -negativeRail * std::tanh((-x) / negativeRail);
    }

public:
    LineDriveCore()
    {
        odFeedbackDiodes.setSpec(rbcomponents::diode1S2473());
        odFeedbackDiodes.setSeries(1, 2); // D7 versus D8+D9
        distHardClipper.setSpec(rbcomponents::diode1S2473());
        distHardClipper.setSourceR(1000.0f); // R23
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        odOpamp.setSampleRate(sampleRate);
        distOpamp.setSampleRate(sampleRate);
        mixOpamp.setSampleRate(sampleRate);
        toneOpamp.setSampleRate(sampleRate);

        inputC22.setRC(sampleRate, 1010000.0f, 47.0e-9f);// R28+R33
        inputBufferPole.setFrequency(sampleRate, 18000.0f);

        odInputC27.setRC(sampleRate, 100000.0f, 47.0e-9f);// R35/C27
        odGainLegC23.setRC(sampleRate, 100.0f, 4.7e-6f);// R39/C23
        odOutputC5.setRC(sampleRate, 20000.0f, 10.0e-6f);// C5/R2
        odMixC4.setRC(sampleRate, 20000.0f, 10.0e-9f);// R2/C4

        distInputC28.setRC(sampleRate, 220000.0f, 22.0e-9f);// R36/C28
        distGainLegC19.setRC(sampleRate, 1200.0f, 2.2e-6f);// R31/C19
        distTrebleLegC18.setRC(sampleRate, 100.0f, 0.47e-6f);// R30/C18
        distOutputC15.setRC(sampleRate, 1000.0f, 10.0e-6f);// C15/R23
        distSeriesC16.setRC(sampleRate, 9000.0f, 18.0e-9f);// C16/load
        distBodyC20.setRC(sampleRate, 6800.0f, 100.0e-9f);// R27/C20
        distBufferC10.setRC(sampleRate, 1000000.0f, 1.0e-6f);// C10/Q2
        distBufferC9.setRC(sampleRate, 20000.0f, 10.0e-6f);// C9/R13
        distOutputC8.setRC(sampleRate, 20000.0f, 820.0e-12f);// R13/C8

        toneDark.setFrequency(sampleRate, 1350.0f);
        tonePresence.setRC(sampleRate, 4700.0f, 10.0e-9f);// R1/C1
        toneCeiling.setRC(sampleRate, 10000.0f, 1.0e-9f);// R7/C6
        toneOutputC7.setRC(sampleRate, 50000.0f, 10.0e-6f);
        finalOutputC11.setRC(sampleRate, 100000.0f, 1.0e-6f);
        outputBufferPole.setFrequency(sampleRate, 36000.0f);
        reset();
    }

    void reset()
    {
        inputC22.reset(); inputBufferPole.reset();
        odInputC27.reset(); odGainLegC23.reset(); odFeedbackC26.reset();
        odOutputC5.reset(); odMixC4.reset();
        distInputC28.reset(); distGainLegC19.reset(); distTrebleLegC18.reset();
        distFeedbackC17.reset(); distOutputC15.reset(); distSeriesC16.reset();
        distBodyC20.reset(); distBufferC10.reset(); distBufferC9.reset();
        distOutputC8.reset();
        toneDark.reset(); tonePresence.reset(); toneCeiling.reset();
        toneOutputC7.reset(); finalOutputC11.reset(); outputBufferPole.reset();
        odOpamp.reset(); distOpamp.reset(); mixOpamp.reset(); toneOpamp.reset();
        odFeedbackDiodes.reset(); distHardClipper.reset();
        updateControls();
    }

    void setParams(float newDrive, float newTone, float newColor, float newLevel)
    {
        drive = clamp01(newDrive);
        tone = clamp01(newTone);
        color = clamp01(newColor);
        level = clamp01(newLevel);
        updateControls();
    }

    float process(float input)
    {
        float x = inputC22.process(input);
        x = inputBufferPole.process(0.992f * x); // Q4/Q6 buffers and switch

        const float od = processOverdrive(x);
        const float distortion = processDistortion(x);

        // VR4 never fully removes the opposite path because both sides load the
        // common IC1a node through R2/R5 and the 20k track.
        const float c = clamp01(color);
        const float odWeight = 0.12f + 0.88f * std::cos(0.5f * kPi * c);
        const float distWeight = 0.12f + 0.88f * std::sin(0.5f * kPi * c);
        // Q2 and the IC1a summing resistors recover the larger passive loss in
        // the hard-clipped branch, keeping Color from acting as a level knob.
        const float mixed = odWeight * od + 1.40f * distWeight * distortion;
        float y = mixOpamp.process(0.64f * mixed, 7.8f); // IC1a R3/R14
        y = processTone(y);
        y = toneOutputC7.process(y);

        y *= outputGain;
        y = outputBufferPole.process(q3EmitterFollower(y));
        return finalOutputC11.process(y);
    }
};

} // namespace linedrive

#endif // LINE_DRIVE_CORE_H
