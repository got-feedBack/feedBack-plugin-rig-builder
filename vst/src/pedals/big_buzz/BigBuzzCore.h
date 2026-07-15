#ifndef BIG_BUZZ_CORE_H
#define BIG_BUZZ_CORE_H

#include <cmath>
#include "../../_shared/semiconductors.hpp"

namespace bigbuzz {

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

class BigBuzzCore
{
    // Version 1 Big Muff / triangle-era schematic from pedals/buzz 2.jpg:
    // Q1-Q4 = 2N5133, D1-D4 = 1N914, Sustain/Tone/Volume = 100 k linear.
    static constexpr float kC1 = 1.0e-6f;
    static constexpr float kR2 = 36000.0f;
    static constexpr float kR14 = 100000.0f;
    static constexpr float kR13 = 39000.0f;
    static constexpr float kR9 = 470000.0f;
    static constexpr float kC10 = 500.0e-12f;
    static constexpr float kC4 = 0.68e-6f;
    static constexpr float kSustainPot = 100000.0f;
    static constexpr float kC5 = 0.68e-6f;
    static constexpr float kR19 = 10000.0f;
    static constexpr float kR20 = 100000.0f;
    static constexpr float kR17 = 470000.0f;
    static constexpr float kC12 = 500.0e-12f;
    static constexpr float kC13 = 1.0e-6f;
    static constexpr float kR12 = 10000.0f;
    static constexpr float kR16 = 100000.0f;
    static constexpr float kR15 = 470000.0f;
    static constexpr float kC11 = 500.0e-12f;
    static constexpr float kC9 = 0.004e-6f;
    static constexpr float kC8 = 0.01e-6f;
    static constexpr float kR5 = 27000.0f;
    static constexpr float kR8 = 27000.0f;
    static constexpr float kTonePot = 100000.0f;
    static constexpr float kC3 = 0.68e-6f;
    static constexpr float kR7 = 470000.0f;
    static constexpr float kR3 = 100000.0f;
    static constexpr float kC2 = 0.68e-6f;
    static constexpr float kVolumePot = 100000.0f;

    float sampleRate = 48000.0f;
    float sustain = 0.64f;
    float tone = 0.46f;

    RcHighPass inputC1;
    RcHighPass q4ToSustain;
    RcHighPass sustainToQ3;
    RcHighPass q3ToQ2;
    RcHighPass q2ToTone;
    RcHighPass toneToQ1;
    RcHighPass outputC2;
    RcLowPass q4FeedbackCap;
    RcLowPass q3FeedbackCap;
    RcLowPass q2FeedbackCap;
    RcLowPass toneLow;
    RcLowPass toneHighBase;
    RcLowPass q1Load;
    rbcomponents::AntiParallelDiodePair clip1;
    rbcomponents::AntiParallelDiodePair clip2;

    float sagEnv = 0.0f;
    float sagAttack = 0.0f;
    float sagRelease = 0.0f;

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
        const float s = clamp01(sustain);
        const float t = clamp01(tone);

        inputC1.setRC(sampleRate, kR2 + kR14, kC1);
        q4ToSustain.setRC(sampleRate, kSustainPot, kC4);

        // The Sustain pot is a 100 k series/shunt network before C5/R19.
        const float sustainSource = 1000.0f + (1.0f - s) * 99000.0f;
        sustainToQ3.setRC(sampleRate, kR19 + sustainSource + kR20, kC5);
        q3ToQ2.setRC(sampleRate, kR12 + kR16, kC13);
        q2ToTone.setRC(sampleRate, kR5 + kR8 + kTonePot, kC9);
        toneToQ1.setRC(sampleRate, parallel(kR7, kR3), kC3);
        outputC2.setRC(sampleRate, kVolumePot, kC2);

        // 500 pF caps in the collector-base feedback loops reduce high-gain
        // fizz without being plain output low-passes.
        q4FeedbackCap.setRC(sampleRate, kR9, kC10);
        q3FeedbackCap.setRC(sampleRate, kR17, kC12);
        q2FeedbackCap.setRC(sampleRate, kR15, kC11);

        // The V1 tone control blends two fixed passive branches. C8/R8 is the
        // low-pass side (about 590 Hz), while C9/R5 is the high-pass side
        // (about 1.47 kHz). The 100 k pot selects between their outputs; it
        // does not move both cutoff frequencies with the wiper.
        // At the dark end the 100 k wiper and Q1 input load add to R8's 27 k
        // source impedance. The resulting loaded corner is close to 400 Hz.
        toneLow.setRC(sampleRate, kR8 + 13000.0f, kC8);
        // R5 is loaded by the tone pot and Q1's input network at the bright
        // end, reducing its effective 27 k impedance to about 15 k.
        toneHighBase.setRC(sampleRate, 15000.0f, kC9);
        q1Load.setHz(sampleRate, 7200.0f + 500.0f * t);

        // Effective source impedance into the feedback diode pairs changes with
        // sustain because the preceding transistor is driven harder.
        clip1.setSpec(rbcomponents::diode1N914());
        clip2.setSpec(rbcomponents::diode1N914());
        clip1.setSourceR(3300.0f - 1600.0f * s);
        clip2.setSourceR(2700.0f - 1200.0f * s);

        sagAttack = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        sagRelease = 1.0f - std::exp(-1.0f / (0.120f * sampleRate));
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 1.15f);
        const float a = target > sagEnv ? sagAttack : sagRelease;
        sagEnv += a * (target - sagEnv);
    }

    float clippedStage(float x, RcLowPass& feedbackCap, rbcomponents::AntiParallelDiodePair& diodes,
                       float drive, float bias, float rail, float asym)
    {
        // Common-emitter 2N5133 gain stage. In a real Big Muff the transistor itself
        // barely clips — the anti-parallel 1N914 pair sits ACROSS the 470k collector-base
        // feedback resistor and clamps the CLOSED-LOOP swing (the smooth, sustaining clip).
        const float amp = bjtStage(x, drive, bias,
                                   1.45f, 1.18f + asym, rail * 0.82f, rail * 0.72f);
        const float feedbackOutput = diodes.process(2.15f * amp);
        // The 500pF cap (Cf) across the 470k feedback gently rolls off the CLIPPED highs —
        // the Muff smooths/de-fizzes the top of the clip rather than fully bypassing it.
        const float tamed = feedbackCap.process(feedbackOutput); // ~680 Hz feedback rolloff
        return feedbackOutput - 0.68f * (feedbackOutput - tamed);
    }

    float toneStack(float x)
    {
        const float t = clamp01(tone);
        const float low = toneLow.process(x);
        const float highBase = toneHighBase.process(x);
        const float high = x - highBase;

        // A linear 100 k wiper blends the two branch voltages. Their separated
        // corners create the familiar mid scoop at noon without an additional
        // synthetic notch or high-frequency leakage at either end.
        const float passiveNotch = 1.0f - 0.20f * (4.0f * t * (1.0f - t));
        return passiveNotch * ((1.0f - t) * low + 1.65f * t * high);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q4ToSustain.reset();
        sustainToQ3.reset();
        q3ToQ2.reset();
        q2ToTone.reset();
        toneToQ1.reset();
        outputC2.reset();
        q4FeedbackCap.reset();
        q3FeedbackCap.reset();
        q2FeedbackCap.reset();
        toneLow.reset();
        toneHighBase.reset();
        q1Load.reset();
        clip1.reset();
        clip2.reset();
        sagEnv = 0.0f;
        updateComponentValues();
    }

    void setSustain(float v)
    {
        sustain = clamp01(v);
        updateComponentValues();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float s = clamp01(sustain);
        const float rail = 1.42f / (1.0f + 0.18f * sagEnv);

        float x = inputC1.process(0.965f * in);

        // Q4 input booster: 2N5133 with R13/R14/R22 and 470 k + 500 pF feedback.
        const float q4Fb = q4FeedbackCap.process(x);
        x = bjtStage(x - 0.22f * q4Fb, 3.8f + 3.4f * s, -0.018f,
                     1.28f, 1.06f, rail * 0.72f, rail * 0.62f);
        x = q4ToSustain.process(x);

        // Real Sustain pot: mostly drive into Q3, not output volume.
        const float potLoss = 0.10f + 0.90f * s;
        x = sustainToQ3.process(x * potLoss);

        float y = clippedStage(x, q3FeedbackCap, clip1,
                               2.8f + 27.5f * s, -0.040f,
                               rail, 0.10f);
        updateSag(y);
        y = q3ToQ2.process(y);

        y = clippedStage(y, q2FeedbackCap, clip2,
                         2.4f + 20.0f * s, 0.024f,
                         rail, 0.05f);
        y = q2ToTone.process(y);

        y = toneStack(y);
        y = toneToQ1.process(y);

        // Q1 recovery/output transistor into the 100 k Volume pot.
        // Q1 is a recovery stage. The bright branch reaches it with less low
        // frequency energy, leaving more collector headroom for pick attacks.
        y = bjtStage(y, 2.2f - 1.0f * tone, -0.012f,
                     1.16f, 1.05f, rail * 0.76f, rail * 0.62f);
        y = q1Load.process(y);
        y = outputC2.process(y);

        // Q1 and both diode-feedback stages already provide the circuit's
        // limiting. A second broadband tanh here made the bright setting flat
        // and brittle, and the old inverse-Sustain trim made the pedal quieter
        // as its real Sustain control was raised.
        return y * 0.58f;
    }
};

} // namespace bigbuzz

#endif // BIG_BUZZ_CORE_H
