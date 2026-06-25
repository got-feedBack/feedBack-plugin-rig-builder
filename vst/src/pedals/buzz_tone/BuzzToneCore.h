#ifndef BUZZ_TONE_CORE_H
#define BUZZ_TONE_CORE_H

#include <cmath>

namespace buzztone {

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
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / ((sr > 1000.0f) ? sr : 48000.0f);
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
        const float rc = rOhm * cFarad;
        const float s = (sr > 1000.0f) ? sr : 48000.0f;
        a = 1.0f - std::exp(-1.0f / (rc * s));
    }

    void setHz(float sr, float hz)
    {
        const float s = (sr > 1000.0f) ? sr : 48000.0f;
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

class BuzzToneCore
{
    // Captain Fuzzle / Maestro FZ-1A style schematic:
    // 1.5 V rail, Q1/Q2/Q3 = 2N1305 PNP germanium, C1/C4 = 10 nF,
    // C2/C3 = 1 uF, FUZZ = 50 kB in the Q2 bias/feedback network,
    // VOLUME = 50 kB after C4.
    static constexpr float kSupplyV = 1.5f;
    static constexpr float kR1 = 100000.0f;
    static constexpr float kR2 = 1000000.0f;
    static constexpr float kC1 = 10.0e-9f;
    static constexpr float kC2 = 1.0e-6f;
    static constexpr float kC3 = 1.0e-6f;
    static constexpr float kC4 = 10.0e-9f;
    static constexpr float kVolumePot = 50000.0f;

    float sampleRate = 48000.0f;
    float fuzz = 0.78f;

    RcHighPass inputCoupling;   // R1/C1/R2 input network
    RcHighPass q1ToQ2;          // C2 into low-impedance Q2 bias network
    RcHighPass q2ToQ3;          // C3 into Q3 base bias
    RcHighPass outputCoupling;  // C4 into 50 k volume pot
    RcLowPass q1Miller;
    RcLowPass q2Miller;
    RcLowPass q3Miller;
    RcLowPass cableLoad;
    DcBlock q1Dc;
    DcBlock q2Dc;
    DcBlock q3Dc;

    float sagEnv = 0.0f;
    float sagAttack = 0.0f;
    float sagRelease = 0.0f;

    static inline float geRail(float v, float posK, float negK, float railPos, float railNeg)
    {
        if (v >= 0.0f)
            return railPos * (1.0f - std::exp(-posK * v));
        return -railNeg * (1.0f - std::exp(negK * v));
    }

    static inline float geStage(float x, float drive, float bias, float posK,
                                float negK, float railPos, float railNeg)
    {
        const float idle = geRail(bias, posK, negK, railPos, railNeg);
        return geRail(bias + drive * x, posK, negK, railPos, railNeg) - idle;
    }

    void updateComponentValues()
    {
        // C1 10 nF into R2 1 M with R1 100 k source isolation: ~14 Hz, but the
        // 100 k resistor deliberately loads the guitar and dulls the attack.
        inputCoupling.setRC(sampleRate, kR1 + kR2, kC1);

        // C2/C3 1 uF see the low-k bias dividers around Q2/Q3, so their audible
        // high-pass corners are in the 25-40 Hz region, not subsonic.
        q1ToQ2.setRC(sampleRate, 4700.0f, kC2);
        q2ToQ3.setRC(sampleRate, 5000.0f, kC3);

        // C4 10 nF into the 50 k volume pot is a major part of the thin,
        // raspy FZ-1A voice: fc ~= 318 Hz.
        outputCoupling.setRC(sampleRate, kVolumePot, kC4);

        // Stray capacitance / transistor Miller equivalent. The real pedal is
        // bright but not alias-fizzy; corners move slightly down as FUZZ rises.
        const float f = clamp01(fuzz);
        q1Miller.setHz(sampleRate, 7200.0f - 1200.0f * f);
        q2Miller.setHz(sampleRate, 5200.0f - 1800.0f * f);
        q3Miller.setHz(sampleRate, 6800.0f - 1100.0f * f);
        cableLoad.setHz(sampleRate, 6200.0f);

        sagAttack = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        sagRelease = 1.0f - std::exp(-1.0f / (0.090f * sampleRate));
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 0.75f);
        const float a = target > sagEnv ? sagAttack : sagRelease;
        sagEnv += a * (target - sagEnv);
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
        q1ToQ2.reset();
        q2ToQ3.reset();
        outputCoupling.reset();
        q1Miller.reset();
        q2Miller.reset();
        q3Miller.reset();
        cableLoad.reset();
        q1Dc.reset();
        q2Dc.reset();
        q3Dc.reset();
        sagEnv = 0.0f;
        updateComponentValues();
    }

    void setFuzz(float v)
    {
        fuzz = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float f = clamp01(fuzz);
        const float f2 = f * f;

        // Battery starvation is not an EQ. It reduces the 1.5 V headroom and
        // shifts bias under sustained drive, producing the real spitty decay.
        const float supplyScale = 1.0f / (1.0f + 0.42f * sagEnv);
        const float rail = 0.68f * kSupplyV * supplyScale;

        float x = inputCoupling.process(0.91f * in); // R1/R2 divider loss

        // Q1: 2N1305 common-emitter input amp (R3 10 k collector load).
        // Low rail + germanium leakage give asymmetry before the fuzz network.
        x = geStage(x, 8.5f + 18.0f * f, -0.19f, 1.45f, 2.25f,
                    rail * 0.74f, rail * 0.58f);
        x = q1Miller.process(q1Dc.process(x));
        x = q1ToQ2.process(x);

        // Q2: main FUZZ/bias/feedback stage. The 50 kB pot changes the effective
        // emitter feedback around R5/R6 and the base bias, so it changes texture
        // and gating more than level.
        float y = geStage(x, 5.0f + 38.0f * f2, -0.11f - 0.13f * f,
                          1.65f + 0.45f * f, 2.55f + 0.75f * f,
                          rail * (0.72f - 0.08f * f), rail * (0.62f + 0.04f * f));
        updateSag(y);
        y = q2Miller.process(q2Dc.process(y));
        y = q2ToQ3.process(y);

        // Q3: fixed recovery/output 2N1305. It clips less than Q2 but folds the
        // already-square wave into the raspy 1.5 V output stage.
        y = geStage(y + 0.045f * x, 4.2f + 7.8f * f, -0.075f,
                    1.35f, 2.05f, rail * 0.70f, rail * 0.56f);
        y = q3Miller.process(q3Dc.process(y));

        y = outputCoupling.process(y);
        y = cableLoad.process(y);

        // Leave level normalization to the wrapper's RBAutoMakeup; this trim
        // keeps the makeup ratio in a stable range before the real Volume pot.
        return y * (0.78f + 0.16f * f);
    }
};

} // namespace buzztone

#endif // BUZZ_TONE_CORE_H
