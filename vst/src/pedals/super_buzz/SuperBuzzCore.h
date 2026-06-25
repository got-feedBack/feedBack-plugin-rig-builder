#ifndef SUPER_BUZZ_CORE_H
#define SUPER_BUZZ_CORE_H

#include <cmath>
#include "../../_shared/semiconductors.hpp"

namespace superbuzz {

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

class SuperBuzzCore
{
    // Univox Super-Fuzz schematic from pedals/buzz 1.gif:
    // 6x 2SC828(Q), OA-90 diode pair, EXPANDER 50 kB, tone switch, BALANCE 50 kB.
    static constexpr float kCInput = 10.0e-6f;
    static constexpr float kRInput = 22000.0f;
    static constexpr float kRBias = 100000.0f;
    static constexpr float kCStage = 10.0e-6f;
    static constexpr float kExpander = 50000.0f;
    static constexpr float kToneCapBright = 1.0e-9f;
    static constexpr float kToneCapDark = 0.1e-6f;
    static constexpr float kToneR1 = 47000.0f;
    static constexpr float kToneR2 = 22000.0f;
    static constexpr float kToneR3 = 10000.0f;
    static constexpr float kBalance = 50000.0f;
    static constexpr float kOutputLoad = 100000.0f;

    float sampleRate = 48000.0f;
    float expander = 0.62f;
    bool toneBright = true;

    RcHighPass inputCoupling;
    RcHighPass q2Coupling;
    RcHighPass expanderCoupling;
    RcHighPass splitterUpperC;
    RcHighPass splitterLowerC;
    RcHighPass diodeToToneA;
    RcHighPass diodeToToneB;
    RcHighPass balanceToQ6;
    RcHighPass outputCoupling;
    RcLowPass q1Miller;
    RcLowPass q2Miller;
    RcLowPass q3Miller;
    RcLowPass splitterMiller;
    RcLowPass toneBrightCap;
    RcLowPass toneDarkCap;
    RcLowPass outputLoad;
    DcBlock q1Dc;
    DcBlock q2Dc;
    DcBlock q3Dc;
    DcBlock splitDc;
    DcBlock q6Dc;
    rbcomponents::AntiParallelDiodePair oa90;

    float sagEnv = 0.0f;
    float sagAttack = 0.0f;
    float sagRelease = 0.0f;

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
        const float e = clamp01(expander);
        inputCoupling.setRC(sampleRate, kRInput + kRBias, kCInput);
        q2Coupling.setRC(sampleRate, 10000.0f + 470000.0f, kCStage);
        expanderCoupling.setRC(sampleRate, kExpander, kCStage);
        splitterUpperC.setRC(sampleRate, 470.0f + 22000.0f, kCStage);
        splitterLowerC.setRC(sampleRate, 470.0f + 22000.0f, kCStage);
        diodeToToneA.setRC(sampleRate, kToneR1 + kToneR2, kCStage);
        diodeToToneB.setRC(sampleRate, kToneR2 + kToneR3, kCStage);
        balanceToQ6.setRC(sampleRate, kBalance, kCStage);
        outputCoupling.setRC(sampleRate, kOutputLoad, kCStage);

        q1Miller.setHz(sampleRate, 8200.0f - 900.0f * e);
        q2Miller.setHz(sampleRate, 6800.0f - 1300.0f * e);
        q3Miller.setHz(sampleRate, 6200.0f - 1600.0f * e);
        splitterMiller.setHz(sampleRate, 7200.0f - 1300.0f * e);
        toneBrightCap.setRC(sampleRate, kToneR3, kToneCapBright);
        toneDarkCap.setRC(sampleRate, kToneR1 + kToneR2, kToneCapDark);
        outputLoad.setHz(sampleRate, 9800.0f);
        oa90.setSpec(rbcomponents::diodeOA90());
        oa90.setSourceR(2500.0f - 1500.0f * e);

        sagAttack = 1.0f - std::exp(-1.0f / (0.010f * sampleRate));
        sagRelease = 1.0f - std::exp(-1.0f / (0.090f * sampleRate));
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 0.95f);
        const float a = target > sagEnv ? sagAttack : sagRelease;
        sagEnv += a * (target - sagEnv);
    }

    float toneNetwork(float x)
    {
        if (toneBright)
        {
            const float cap = toneBrightCap.process(x);
            return 0.92f * (x - 0.78f * cap) + 0.08f * x;
        }

        const float dark = toneDarkCap.process(x);
        const float nasal = x - 0.44f * dark;
        return 0.82f * dark + 0.24f * nasal;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        inputCoupling.reset();
        q2Coupling.reset();
        expanderCoupling.reset();
        splitterUpperC.reset();
        splitterLowerC.reset();
        diodeToToneA.reset();
        diodeToToneB.reset();
        balanceToQ6.reset();
        outputCoupling.reset();
        q1Miller.reset();
        q2Miller.reset();
        q3Miller.reset();
        splitterMiller.reset();
        toneBrightCap.reset();
        toneDarkCap.reset();
        outputLoad.reset();
        q1Dc.reset();
        q2Dc.reset();
        q3Dc.reset();
        splitDc.reset();
        q6Dc.reset();
        oa90.reset();
        sagEnv = 0.0f;
        updateComponentValues();
    }

    void setExpander(float v)
    {
        expander = clamp01(v);
        updateComponentValues();
    }

    void setToneSwitch(float v)
    {
        toneBright = v >= 0.5f;
    }

    float process(float in)
    {
        const float e = clamp01(expander);
        const float rail = 1.44f / (1.0f + 0.20f * sagEnv);

        float x = inputCoupling.process(0.96f * in);

        // Q1/Q2 2SC828 preamp and feedback network before the Expander pot.
        x = bjtStage(x, 4.4f + 3.2f * e, -0.026f,
                     1.22f, 1.05f, rail * 0.74f, rail * 0.62f);
        x = q1Miller.process(q1Dc.process(x));
        x = q2Coupling.process(x);

        x = bjtStage(x, 3.7f + 4.6f * e, 0.018f,
                     1.34f, 1.12f, rail * 0.78f, rail * 0.66f);
        x = q2Miller.process(q2Dc.process(x));

        // EXPANDER 50 kB: drive into Q3 and the phase-split octave pair.
        x = expanderCoupling.process(x * (0.08f + 0.92f * e));
        x = bjtStage(x, 4.2f + 10.0f * e, -0.032f,
                     1.42f + 0.28f * e, 1.18f + 0.18f * e,
                     rail * 0.78f, rail * 0.68f);
        x = q3Miller.process(q3Dc.process(x));

        const float upperIn = splitterUpperC.process(x);
        const float lowerIn = splitterLowerC.process(-x);
        const float upper = bjtStage(upperIn, 5.6f + 11.0f * e, 0.024f,
                                     1.35f, 1.12f, rail * 0.74f, rail * 0.66f);
        const float lower = bjtStage(lowerIn, 5.2f + 10.0f * e, 0.022f,
                                     1.32f, 1.10f, rail * 0.74f, rail * 0.66f);

        // The push-pull pair is recombined into a strong full-wave octave; a
        // controlled amount of fundamental leakage keeps it guitar-like.
        float octave = upper + lower;
        const float fundamental = upper - lower;
        octave = splitterMiller.process(splitDc.process(octave));
        float y = 0.24f * fundamental + (0.64f + 0.56f * e) * octave;

        updateSag(y);
        y = oa90.process(2.1f * y);
        y = diodeToToneA.process(y);
        y = diodeToToneB.process(y);
        y = toneNetwork(y);

        y = balanceToQ6.process(y);
        y = bjtStage(y, 2.8f + 1.0f * e, -0.010f,
                     1.18f, 1.04f, rail * 0.76f, rail * 0.62f);
        y = outputLoad.process(q6Dc.process(y));
        y = outputCoupling.process(y);

        const float trim = 0.46f / (1.0f + 0.18f * e);
        return std::tanh(y * trim);
    }
};

} // namespace superbuzz

#endif // SUPER_BUZZ_CORE_H
