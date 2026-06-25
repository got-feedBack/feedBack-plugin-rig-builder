#ifndef BZ1_CORE_H
#define BZ1_CORE_H

#include <cmath>

namespace bz1 {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float smoothTaper(float v)
{
    const float x = clamp01(v);
    return x * x * (3.0f - 2.0f * x);
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
    void setHz(float sr, float hz)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        a = 1.0f - std::exp(-2.0f * kPi * hz / s);
    }

    void setRC(float sr, float rOhm, float cFarad)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float rc = rOhm * cFarad;
        a = 1.0f - std::exp(-1.0f / (rc * s));
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

class BZ1Core
{
    // Boss FZ-3 / Aion Argent style schematic:
    // Original Boss parts are Q1 = 2SK184-GR JFET input buffer and
    // Q2-Q8 = 2SC2458-LG/GR silicon stages. Aion's 2SK209-GR/2N5088
    // substitutions stay close, but this voicing is biased toward the original
    // Toshiba parts now present in componentes/.
    // FUZZ = 1 kC, TONE = 100 kW Muff-derived network, VOLUME = 100 kA.
    static constexpr float kR1 = 10000.0f;
    static constexpr float kR2 = 1000000.0f;
    static constexpr float kC1 = 47.0e-9f;
    static constexpr float kC2 = 220.0e-9f;
    static constexpr float kC3 = 27.0e-9f;
    static constexpr float kR4 = 12000.0f;
    static constexpr float kR5 = 47000.0f;
    static constexpr float kC5 = 1.0e-6f;
    static constexpr float kR12 = 100000.0f;
    static constexpr float kC7 = 10.0e-6f;
    static constexpr float kC8 = 18.0e-9f;
    static constexpr float kR16 = 150000.0f;
    static constexpr float kR17 = 330000.0f;
    static constexpr float kC10 = 47.0e-9f;
    static constexpr float kC11 = 10.0e-9f;
    static constexpr float kC12 = 10.0e-9f;
    static constexpr float kR20 = 47000.0f;
    static constexpr float kR21 = 47000.0f;
    static constexpr float kR22 = 47000.0f;
    static constexpr float kC13 = 1.0e-6f;
    static constexpr float kR23 = 100000.0f;
    static constexpr float kC14 = 10.0e-6f;
    static constexpr float kR25 = 100000.0f;

    float sampleRate = 48000.0f;
    float fuzz = 0.70f;
    float tone = 0.50f;

    RcHighPass inputC1;
    RcHighPass jfetToQ2;
    RcHighPass q3ToFuzz;
    RcHighPass q5ToQ6;
    RcHighPass q6ToTone;
    RcHighPass toneToBuffer;
    RcHighPass outputC14;
    RcLowPass q1Stray;
    RcLowPass q2Miller;
    RcLowPass q3Miller;
    RcLowPass q4Miller;
    RcLowPass q5Miller;
    RcLowPass q6Miller;
    RcLowPass q7Load;
    RcLowPass fuzzBypass;
    RcLowPass toneLow;
    RcLowPass toneHigh;
    RcLowPass toneBody;
    DcBlock q2Dc;
    DcBlock q3Dc;
    DcBlock q4Dc;
    DcBlock q5Dc;
    DcBlock q6Dc;

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
        const float f = clamp01(fuzz);
        const float t = clamp01(tone);

        inputC1.setRC(sampleRate, kR1 + kR2, kC1);
        jfetToQ2.setRC(sampleRate, parallel(kR4, kR5), kC2 + kC3);
        q3ToFuzz.setRC(sampleRate, kR12, kC5);
        q5ToQ6.setRC(sampleRate, parallel(kR16, kR17), kC8);
        q6ToTone.setRC(sampleRate, kR20 + kR21, kC10);
        toneToBuffer.setRC(sampleRate, kR23, kC13);
        outputC14.setRC(sampleRate, kR25, kC14);

        q1Stray.setHz(sampleRate, 11800.0f);
        q2Miller.setHz(sampleRate, 7600.0f - 1300.0f * f);
        q3Miller.setHz(sampleRate, 6500.0f - 1600.0f * f);
        q4Miller.setHz(sampleRate, 5600.0f - 1900.0f * f);
        q5Miller.setHz(sampleRate, 6100.0f - 1600.0f * f);
        q6Miller.setHz(sampleRate, 8800.0f - 1200.0f * f);
        q7Load.setHz(sampleRate, 15500.0f);

        const float fuzzR = 950.0f - 830.0f * smoothTaper(f);
        fuzzBypass.setRC(sampleRate, fuzzR < 120.0f ? 120.0f : fuzzR, kC7);

        toneLow.setRC(sampleRate, kR21 + kR22 + 80000.0f * (1.0f - t), kC12);
        toneHigh.setRC(sampleRate, kR20 + 100000.0f * t, kC11);
        toneBody.setHz(sampleRate, 1200.0f + 1300.0f * t);

        sagAttack = 1.0f - std::exp(-1.0f / (0.010f * sampleRate));
        sagRelease = 1.0f - std::exp(-1.0f / (0.085f * sampleRate));
    }

    void updateSag(float x)
    {
        const float target = clamp01(std::fabs(x) * 0.55f);
        const float a = target > sagEnv ? sagAttack : sagRelease;
        sagEnv += a * (target - sagEnv);
    }

    inline float jfetBuffer(float x)
    {
        const float y = std::tanh(1.35f * x + 0.02f) - std::tanh(0.02f);
        return q1Stray.process(0.93f * x + 0.07f * y);
    }

    float toneStack(float x)
    {
        const float t = clamp01(tone);
        const float tw = smoothTaper(t);
        const float low = toneLow.process(x);
        const float highBase = toneHigh.process(x);
        const float high = x - 0.82f * highBase;
        const float body = toneBody.process(x);
        const float scoop = 0.18f + 0.20f * (1.0f - std::fabs(2.0f * t - 1.0f));

        return low * (1.16f - 0.88f * tw)
             + high * (0.16f + 1.18f * tw)
             - body * scoop;
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
        jfetToQ2.reset();
        q3ToFuzz.reset();
        q5ToQ6.reset();
        q6ToTone.reset();
        toneToBuffer.reset();
        outputC14.reset();
        q1Stray.reset();
        q2Miller.reset();
        q3Miller.reset();
        q4Miller.reset();
        q5Miller.reset();
        q6Miller.reset();
        q7Load.reset();
        fuzzBypass.reset();
        toneLow.reset();
        toneHigh.reset();
        toneBody.reset();
        q2Dc.reset();
        q3Dc.reset();
        q4Dc.reset();
        q5Dc.reset();
        q6Dc.reset();
        sagEnv = 0.0f;
        updateComponentValues();
    }

    void setFuzz(float v)
    {
        fuzz = clamp01(v);
        updateComponentValues();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float f = clamp01(fuzz);
        const float f2 = f * f;
        const float supply = 1.0f / (1.0f + 0.18f * sagEnv);
        const float railA = 1.55f * supply;
        const float railC = 1.36f * supply;

        float x = inputC1.process(0.965f * in);
        x = jfetBuffer(x);
        x = jfetToQ2.process(x);

        float y = bjtStage(x, 4.2f + 4.6f * f, -0.035f,
                           1.25f + 0.45f * f, 1.05f + 0.22f * f,
                           railC * 0.72f, railC * 0.62f);
        y = q2Miller.process(q2Dc.process(y));

        y = bjtStage(y, 3.4f + 7.0f * f, 0.022f,
                     1.42f + 0.70f * f, 1.22f + 0.38f * f,
                     railC * 0.76f, railC * 0.67f);
        y = q3Miller.process(q3Dc.process(y));
        y = q3ToFuzz.process(y);

        const float bypass = fuzzBypass.process(y);
        const float fuzzDrive = 0.72f * y + (0.44f + 1.10f * smoothTaper(f)) * bypass;
        y = bjtStage(fuzzDrive, 5.8f + 23.0f * f2, -0.060f - 0.025f * f,
                     1.55f + 1.20f * f, 1.22f + 0.82f * f,
                     railA * (0.74f - 0.06f * f), railA * (0.66f + 0.04f * f));
        updateSag(y);
        y = q4Miller.process(q4Dc.process(y));

        y = bjtStage(y + 0.18f * fuzzDrive, 3.6f + 13.0f * f,
                     0.034f + 0.012f * f,
                     1.28f + 0.92f * f, 1.08f + 0.62f * f,
                     railA * 0.78f, railA * 0.68f);
        y = q5Miller.process(q5Dc.process(y));
        y = q5ToQ6.process(y);

        y = bjtStage(y, 2.5f + 3.8f * f, -0.020f,
                     1.18f + 0.35f * f, 1.04f + 0.22f * f,
                     railA * 0.78f, railA * 0.64f);
        y = q6Miller.process(q6Dc.process(y));
        y = q6ToTone.process(y);

        y = toneStack(y);
        y = toneToBuffer.process(y);
        y = q7Load.process(std::tanh(1.08f * y) * 0.93f);
        y = outputC14.process(y);

        return y * (0.54f + 0.08f * f);
    }
};

} // namespace bz1

#endif // BZ1_CORE_H
