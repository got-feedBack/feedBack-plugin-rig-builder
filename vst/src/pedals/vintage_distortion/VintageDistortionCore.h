#ifndef VINTAGE_DISTORTION_CORE_H
#define VINTAGE_DISTORTION_CORE_H

/*
 * DOD 250 Overdrive/Preamp core derived from pedals/vintage distortion.png.
 * Runs at the wrapper's 4x sample rate.
 *
 * C2/R6 -> LM741 non-inverting stage with R8/C5 feedback and the real
 * C3/R7/VR1 gain leg -> C4/R9 -> D1 versus D2+D3 asymmetric shunt clipper ->
 * C6 -> VR2 Volume.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace vintagedistortion {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
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
        y1 = std::fabs(y) < 1.0e-15f ? 0.0f : y;
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
        if (std::fabs(state) < 1.0e-15f)
            state = 0.0f;
        return state;
    }
};

class Lm741OutputStage
{
    const rbshared::OpAmpSpec spec = rbshared::lm741Spec();
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

        // The 9 V supply is biased at 4.5 V. This is the LM741 output swing,
        // applied before its finite bandwidth and slew response.
        const float positiveRail = 3.0f / spec.voltsPerUnit;
        const float negativeRail = 2.7f / spec.voltsPerUnit;
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
        state = std::fabs(next) < 1.0e-15f ? 0.0f : next;
        return state;
    }
};

class VintageDistortionCore
{
    static constexpr float kFeedbackR = 1000000.0f; // R8
    static constexpr float kGainR = 4700.0f;        // R7
    static constexpr float kGainPot = 500000.0f;    // VR1

    float sampleRate = 192000.0f;
    float gain = 0.35f;
    float volume = 0.62f;
    float gainResistance = kGainR + kGainPot;
    float noiseGain = 1.0f;
    float outputGain = 0.0f;

    RcHighPass inputC2;
    RcHighPass gainLegC3;
    RcHighPass outputC4;
    RcLowPass feedbackC5;
    RcLowPass clipperC6;
    Lm741OutputStage lm741;
    rbcomponents::AsymDiodeStringClipper outputDiodes;

    void updateControls()
    {
        // VR1 is wired as a rheostat in the inverting leg. Its reverse-log
        // effective law spreads the 3x-to-214x electrical gain over the knob.
        gainResistance = kGainR + kGainPot * audioTaper(1.0f - gain);
        noiseGain = 1.0f + kFeedbackR / gainResistance;
        gainLegC3.setRC(sampleRate, gainResistance, 4.7e-9f);

        // VR2 is the passive 100k output pot. The fixed normalization factor
        // matches the project's instrument-level convention; it is not a
        // gain-dependent makeup stage.
        outputGain = 4.0f * audioTaper(volume);
    }

public:
    VintageDistortionCore()
    {
        outputDiodes.setSpec(rbcomponents::diode1N4148());
        outputDiodes.setSeries(1, 2); // D1 versus D2+D3
        outputDiodes.setSourceR(10000.0f); // R9
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        lm741.setSampleRate(sampleRate);

        inputC2.setRC(sampleRate, 2210000.0f, 10.0e-9f); // R5+R6/C2
        outputC4.setRC(sampleRate, 110000.0f, 4.7e-6f);  // R9+VR2/C4
        feedbackC5.setRC(sampleRate, 1000000.0f, 22.0e-12f);
        clipperC6.setRC(sampleRate, 10000.0f, 1.0e-9f);
        reset();
    }

    void reset()
    {
        inputC2.reset();
        gainLegC3.reset();
        outputC4.reset();
        feedbackC5.reset();
        clipperC6.reset();
        lm741.reset();
        outputDiodes.reset();
        updateControls();
    }

    void setParams(float newGain, float newVolume)
    {
        gain = clamp01(newGain);
        volume = clamp01(newVolume);
        updateControls();
    }

    float process(float input)
    {
        const float x = inputC2.process(input);

        // Non-inverting gain: 1 + R8/Z(C3 + R7 + VR1). C5 across R8 limits
        // only the boosted feedback component, preserving the unity path.
        const float highPassed = gainLegC3.process(x);
        const float boost = feedbackC5.process((noiseGain - 1.0f) * highPassed);
        float y = lm741.process(x + boost, noiseGain);

        // C4 removes the 4.5 V bias. R9 then drives the sole nonlinear diode
        // node; C6 is its real 1 nF shunt capacitor.
        y = outputC4.process(y);
        y = outputDiodes.process(3.0f * y) / 3.0f;
        y = clipperC6.process(y);
        return y * outputGain;
    }
};

} // namespace vintagedistortion

#endif // VINTAGE_DISTORTION_CORE_H
