#ifndef RAT_CORE_H
#define RAT_CORE_H

/*
 * Pro Co RAT II core, derived from pedals/Proco-Rat-II-Schematic.png.
 * Runs at the wrapper's 4x sample rate.
 *
 * Audible path:
 * C3/R4/R5 input coupling -> LM308 non-inverting stage with the two real
 * frequency-dependent gain legs (R7/C6 and R8/C7) -> R9/C11 -> 1N4148 shunt
 * clipper -> VR2/R10/C12 Filter -> 2N5458 source follower -> C14/VR3 Volume.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace rat {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float audioTaper(float v)
{
    // Standard 20 dB audio taper: about 9.1% electrical travel at noon.
    return (std::pow(10.0f, 2.0f * clamp01(v)) - 1.0f) / 99.0f;
}

static inline float distortionTaper(float v)
{
    // The RAT gain control uses a much steeper effective law. A 36 dB curve
    // yields 1.56% travel at noon, matching the measured gain sweep while
    // retaining more useful resistance below noon than a simple x^6 fit.
    static const float span = 3980.0717f; // 10^3.6 - 1
    return (std::pow(10.0f, 3.6f * clamp01(v)) - 1.0f) / span;
}

static inline float onePoleCoef(float fc, float fs)
{
    const float c = 1.0f - std::exp(-2.0f * kPi * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class OnePoleLowPass
{
    float coefficient = 0.0f;
    float state = 0.0f;

public:
    void setFrequency(float hz, float sampleRate)
    {
        coefficient = onePoleCoef(hz, sampleRate);
    }

    void reset() { state = 0.0f; }

    float process(float x)
    {
        state += coefficient * (x - state);
        return state;
    }
};

class OnePoleHighPass
{
    float coefficient = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setFrequency(float hz, float sampleRate)
    {
        const float dt = 1.0f / sampleRate;
        const float rc = 1.0f / (2.0f * kPi * hz);
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

class Lm308OutputStage
{
    rbshared::OpAmpSpec spec = rbshared::lm308Spec();
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
        const float pole = spec.gbwHz / gain;
        const float coefficient = onePoleCoef(pole < 20.0f ? 20.0f : pole,
                                               sampleRate);
        // The RAT biases the 9 V supply at 4.5 V. The diode load through R9
        // lets the LM308 approach the supply rails more closely than it would
        // into a low-value resistive load.
        const float pos = 4.1f / spec.voltsPerUnit;
        const float neg = 4.1f / spec.voltsPerUnit;
        const float desired = target >= 0.0f
            ? pos * std::tanh(target / pos)
            : -neg * std::tanh((-target) / neg);

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

class RatCore
{
    float sampleRate = 192000.0f;
    float distortionResistance = 0.0f;
    float filterResistance = 1500.0f;
    float outputGain = 0.0f;
    float noiseGain = 1.0f;

    OnePoleHighPass inputCoupling;
    OnePoleLowPass gainLeg60Low;
    OnePoleLowPass gainLeg1540Low;
    OnePoleLowPass feedbackCompensation;
    OnePoleLowPass filterLowPass;
    OnePoleHighPass outputCoupling;
    Lm308OutputStage lm308;
    rbcomponents::AntiParallelDiodePair clipper;

    void updateControls(float distortion, float filter, float volume)
    {
        // All three controls are 100k pots. VR1 uses the steep effective law
        // measured from the gain references; VR2 and VR3 retain their own
        // filter and level tapers instead of sharing one generic knob curve.
        distortionResistance = 100000.0f * distortionTaper(distortion);
        filterResistance = 1500.0f + 100000.0f * std::pow(clamp01(filter), 3.0f);
        outputGain = 3.85f * audioTaper(volume);

        // Around the guitar's upper fundamentals, C6/C7 keep the effective
        // impedance above the bare 44R resistor parallel. 62R is the calibrated
        // band-average used by the LM308 noise-gain bandwidth model.
        noiseGain = 1.0f + distortionResistance / 62.0f;

        // C5=100p across VR1 and C10=33p on the LM308 prevent ultrasonic gain.
        const float feedbackPole = distortionResistance > 100.0f
            ? 1.0f / (2.0f * kPi * distortionResistance * 100.0e-12f)
            : sampleRate * 0.45f;
        feedbackCompensation.setFrequency(feedbackPole, sampleRate);

        const float filterCutoff = 1.0f / (2.0f * kPi * filterResistance * 3.3e-9f);
        filterLowPass.setFrequency(filterCutoff, sampleRate);
    }

public:
    RatCore()
    {
        clipper.setSpec(rbcomponents::diode1N4148());
        clipper.setSourceR(1000.0f); // R9
    }

    void reset()
    {
        inputCoupling.reset();
        gainLeg60Low.reset();
        gainLeg1540Low.reset();
        feedbackCompensation.reset();
        filterLowPass.reset();
        outputCoupling.reset();
        lm308.reset();
        clipper.reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        lm308.setSampleRate(sampleRate);

        // C3=22n sees R4||R5 = 1.1M. The two feedback branches are exactly
        // 1/(2*pi*560*4.7u) and 1/(2*pi*47*2.2u).
        inputCoupling.setFrequency(6.58f, sampleRate);
        gainLeg60Low.setFrequency(60.47f, sampleRate);
        gainLeg1540Low.setFrequency(1539.3f, sampleRate);
        outputCoupling.setFrequency(8.0f, sampleRate); // C14 into the output loads
        reset();
    }

    void setParams(float distortion, float filter, float volume)
    {
        updateControls(distortion, filter, volume);
    }

    float process(float input)
    {
        const float x = inputCoupling.process(input);
        const float hp60 = x - gainLeg60Low.process(x);
        const float hp1540 = x - gainLeg1540Low.process(x);

        // Non-inverting gain: 1 + VR1/Z(R7+C6) + VR1/Z(R8+C7).
        float target = x
            + (distortionResistance / 560.0f) * hp60
            + (distortionResistance / 47.0f) * hp1540;
        target = feedbackCompensation.process(target);

        float y = lm308.process(target, noiseGain);

        // OpAmpStage expresses its output in normalized audio units, while the
        // diode solver uses physical volts. The LM308 spec is 3 V per unit.
        y = clipper.process(y * 3.0f) / 3.0f;
        y = filterLowPass.process(y);

        // Q5 2N5458 is a source follower. At the diode-limited signal level it
        // contributes loading and a small loss, not another clipping stage.
        y *= 0.985f;
        y = outputCoupling.process(y);
        return y * outputGain;
    }
};

} // namespace rat

#endif // RAT_CORE_H
