#ifndef CUSTOM_DRIVE_CORE_H
#define CUSTOM_DRIVE_CORE_H

/*
 * Fulltone OCD-style core from pedals/custom drive.png and pedals/OCD.pdf.
 * Runs at the wrapper's 4x sample rate.
 *
 * C1/R3 -> TL082 stage 1 with R8+Drive/C6 feedback and R5/C4 ground leg ->
 * R9/C7 -> 2N7000/2N7000/1N34A asymmetric shunt network -> R10 -> TL082
 * stage 2 with R13/C9 and R11/C8 -> C10 -> HP/LP source resistance ->
 * C11/Tone shunt shelf -> Volume.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace customdrive {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float audioTaper(float v)
{
    // A-taper used by the local pedal UI and prior preset calibration.
    return std::pow(clamp01(v), 1.70f);
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

class Tl082OutputStage
{
    const rbshared::OpAmpSpec spec = rbshared::tl082Spec();
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
        const float positiveRail = 3.25f / spec.voltsPerUnit;
        const float negativeRail = 3.05f / spec.voltsPerUnit;
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

class CustomDriveCore
{
    float sampleRate = 192000.0f;
    float drive = 0.22f;
    float tone = 0.50f;
    float voice = 0.0f;
    float volume = 0.62f;
    float firstFeedbackR = 18000.0f;
    float toneHighGain = 0.0f;
    float outputGain = 0.0f;

    RcHighPass inputC1;
    RcHighPass firstGroundC4;
    RcLowPass firstFeedbackC6;
    RcLowPass clipperC7;
    RcHighPass secondGroundC8;
    RcLowPass secondFeedbackC9;
    RcHighPass outputC10;
    RcLowPass toneShelfLow;
    Tl082OutputStage firstOpamp;
    Tl082OutputStage secondOpamp;
    rbcomponents::OcdMosfetGeClipper mosfetGeClipper;

    void updateControls()
    {
        // X2 is A1M in series with R8=18k. It changes only the first-stage
        // feedback resistance; the MOSFET/Ge network remains a fixed circuit.
        firstFeedbackR = 18000.0f + 1000000.0f * audioTaper(drive);
        firstFeedbackC6.setRC(sampleRate, firstFeedbackR, 220.0e-12f);

        // LP uses R15=33k. HP parallels R14=22k, yielding 13.2k. C11 and the
        // linear 10k Tone rheostat form H(s)=(1+s*C*Rt)/(1+s*C*(Rs+Rt)).
        const float sourceR = voice >= 0.5f ? 13200.0f : 33000.0f;
        const float toneR = 100.0f + 9900.0f * clamp01(tone);
        toneHighGain = toneR / (sourceR + toneR);
        toneShelfLow.setRC(sampleRate, sourceR + toneR, 47.0e-9f);

        outputC10.setRC(sampleRate, sourceR + 100000.0f, 10.0e-6f);
        // Calibrated against the common distortion-pedal reference level: at
        // noon Volume/Drive this lands alongside the RAT rather than copying
        // the deliberately quiet OCD reference renders.
        const float highDrive = clamp01(2.0f * drive - 1.0f);
        const float highDriveCurve = highDrive * highDrive * (3.0f - 2.0f * highDrive);
        outputGain = .96f * audioTaper(volume) * (1.0f + .35f * highDriveCurve);
    }

public:
    CustomDriveCore()
    {
        mosfetGeClipper.setSourceR(10000.0f); // R9
        mosfetGeClipper.setHardness(1.0f);    // fixed 2N7000/1N34A network
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        firstOpamp.setSampleRate(sampleRate);
        secondOpamp.setSampleRate(sampleRate);

        inputC1.setRC(sampleRate, 1010000.0f, 22.0e-9f);// R3+R2/C1
        firstGroundC4.setRC(sampleRate, 2200.0f, 100.0e-9f);// R5/C4
        clipperC7.setRC(sampleRate, 10000.0f, 10.0e-9f);// R9/C7
        secondGroundC8.setRC(sampleRate, 39000.0f, 100.0e-9f);// R11/C8
        secondFeedbackC9.setRC(sampleRate, 150000.0f, 220.0e-12f);// R13/C9
        reset();
    }

    void reset()
    {
        inputC1.reset();
        firstGroundC4.reset();
        firstFeedbackC6.reset();
        clipperC7.reset();
        secondGroundC8.reset();
        secondFeedbackC9.reset();
        outputC10.reset();
        toneShelfLow.reset();
        firstOpamp.reset();
        secondOpamp.reset();
        mosfetGeClipper.reset();
        updateControls();
    }

    void setParams(float newDrive, float newTone, float newVoice, float newVolume)
    {
        drive = clamp01(newDrive);
        tone = clamp01(newTone);
        voice = newVoice >= 0.5f ? 1.0f : 0.0f;
        volume = clamp01(newVolume);
        updateControls();
    }

    float process(float input)
    {
        const float x = inputC1.process(input);

        // First non-inverting stage: 1 + (R8+Drive)/Z(R5+C4). C6 limits only
        // the boosted feedback component.
        const float firstHp = firstGroundC4.process(x);
        const float firstBoost = firstFeedbackC6.process(
            (firstFeedbackR / 2200.0f) * firstHp);
        const float firstNoiseGain = 1.0f + firstFeedbackR / 2200.0f;
        float y = firstOpamp.process(x + firstBoost, firstNoiseGain);

        // R9 drives the fixed 2N7000/2N7000/1N34A node. The nonlinear solver
        // receives physical volts; C7 is the real capacitor at that node.
        y = mosfetGeClipper.process(3.0f * y) / 3.0f;
        y = clipperC7.process(y);

        // Second TL082 stage: 1 + R13/Z(R11+C8), with C9 across R13.
        const float secondHp = secondGroundC8.process(y);
        const float secondBoost = secondFeedbackC9.process(
            (150000.0f / 39000.0f) * secondHp);
        y = secondOpamp.process(y + secondBoost, 1.0f + 150000.0f / 39000.0f);
        y = outputC10.process(y);

        // Passive HP/LP and Tone network. This is a low shelf, not a synthetic
        // low/high blend, and contains no gain- or envelope-dependent state.
        const float low = toneShelfLow.process(y);
        y = low + toneHighGain * (y - low);
        return y * outputGain;
    }
};

} // namespace customdrive

#endif // CUSTOM_DRIVE_CORE_H
