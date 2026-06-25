#ifndef ALLOY_DISTORTION_CORE_H
#define ALLOY_DISTORTION_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace alloydistortion {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return hz < 20.0f ? 20.0f : (hz > nyquist ? nyquist : hz);
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
        a = 1.0f - std::exp(-2.0f * kPi * clampFreq(hz, s) / s);
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

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    inline float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = dn(b1 * x - a1 * y + z2);
        z2 = dn(b2 * x - a2 * y);
        return y;
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        hz = clampFreq(hz, s);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / s;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setLowPass(float sr, float hz, float q)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        hz = clampFreq(hz, s);
        const float w0 = 2.0f * kPi * hz / s;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        const float srate = sr > 1000.0f ? sr : 48000.0f;
        hz = clampFreq(hz, srate);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / srate;
        const float c = std::cos(w0);
        const float si = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = si * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class AlloyDistortionCore
{
    // Boss HM-2 schematic anchors from pedals/alloy distortion.pdf:
    // C1 47 nF/R8 1 M input FET buffer, transistor gain into IC1b,
    // C9/R20/D3-D5 feedback soft clipper, D6/D7 germanium-series crossover
    // modeled with the available OA90 datasheet,
    // D8/D9 hard clipper, VR4 250 k Dist, VR2/VR3 10 k Color Mix L/H,
    // and VR1 10 k Level.
    static constexpr float kC1 = 47.0e-9f;
    static constexpr float kR8 = 1000000.0f;
    static constexpr float kC4 = 1.0e-6f;
    static constexpr float kR11 = 1000000.0f;
    static constexpr float kC6 = 47.0e-9f;
    static constexpr float kR15 = 1000000.0f;
    static constexpr float kR16 = 470000.0f;
    static constexpr float kC10 = 100.0e-12f;
    static constexpr float kC11 = 47.0e-9f;
    static constexpr float kR20 = 220000.0f;
    static constexpr float kC9 = 100.0e-12f;
    static constexpr float kC12 = 1.0e-6f;
    static constexpr float kR23 = 10000.0f;
    static constexpr float kC16 = 1.0e-9f;
    static constexpr float kDistPot = 250000.0f;
    static constexpr float kColorPot = 10000.0f;
    static constexpr float kLevelPot = 10000.0f;
    static constexpr float kC30 = 68.0e-9f;
    static constexpr float kC27 = 6.8e-9f;
    static constexpr float kC26 = 4.7e-9f;

    float sampleRate = 48000.0f;
    float dist = 0.55f;
    float colorLow = 0.82f;
    float colorHigh = 0.72f;

    RcHighPass inputC1;
    RcHighPass q1CouplingC4;
    RcHighPass q4CouplingC6;
    RcHighPass q6CouplingC11;
    RcHighPass opampCouplingC12;
    RcHighPass colorLowCap;
    RcHighPass colorHighCap;
    RcHighPass colorPresenceCap;
    RcHighPass outputCoupling;
    RcLowPass inputFetPole;
    RcLowPass q6FeedbackC10;
    RcLowPass opampFeedbackC9;
    RcLowPass hardClipCapC16;
    RcLowPass outputLoad;
    Biquad lowColorBoost;
    Biquad highColorBoost;
    Biquad highColorShelf;
    Biquad postColorLimit;
    DcBlock q6Dc;
    DcBlock opampDc;
    DcBlock geDc;
    rbshared::OpAmpStage ic1b;
    rbshared::OpAmpStage ic2a;
    rbcomponents::AsymDiodeStringClipper feedbackDiodesQ6;
    rbcomponents::AsymDiodeStringClipper feedbackDiodesOpamp;
    rbcomponents::AntiParallelDiodePair germaniumPair;
    rbcomponents::AntiParallelDiodePair hardClipper;

    void updateComponentValues()
    {
        const float d = smoothstep(dist);
        const float lo = clamp01(colorLow);
        const float hi = clamp01(colorHigh);

        inputC1.setRC(sampleRate, kR8, kC1);
        q1CouplingC4.setRC(sampleRate, kR11, kC4);
        q4CouplingC6.setRC(sampleRate, kR15, kC6);
        q6CouplingC11.setRC(sampleRate, 22000.0f + (1.0f - d) * 0.45f * kDistPot, kC11);
        opampCouplingC12.setRC(sampleRate, kR23, kC12);
        colorLowCap.setRC(sampleRate, 330.0f + (1.0f - lo) * kColorPot, kC30);
        colorHighCap.setRC(sampleRate, 330.0f + (1.0f - hi) * kColorPot, kC27);
        colorPresenceCap.setRC(sampleRate, 330.0f + (1.0f - hi) * kColorPot, kC26);
        outputCoupling.setRC(sampleRate, kLevelPot, 1.0e-6f);

        inputFetPole.setHz(sampleRate, 15000.0f);
        q6FeedbackC10.setRC(sampleRate, kR16, kC10);
        opampFeedbackC9.setRC(sampleRate, kR20 + d * kDistPot, kC9);
        hardClipCapC16.setRC(sampleRate, kR23, kC16);
        outputLoad.setHz(sampleRate, 7600.0f + 2800.0f * hi - 600.0f * lo);

        lowColorBoost.setPeaking(sampleRate, 88.0f + 42.0f * lo, 0.70f, -3.0f + 16.0f * lo);
        highColorBoost.setPeaking(sampleRate, 1080.0f + 520.0f * hi, 0.48f, -4.5f + 20.0f * hi);
        highColorShelf.setHighShelf(sampleRate, 2300.0f + 1500.0f * hi, 0.68f, -7.0f + 11.0f * hi);
        postColorLimit.setLowPass(sampleRate, 3600.0f + 4300.0f * hi, 0.58f);

        feedbackDiodesQ6.setSpec(rbcomponents::diode1S2473());
        feedbackDiodesQ6.setSeries(1, 2);
        feedbackDiodesQ6.setSourceR(2600.0f - 1100.0f * d);
        feedbackDiodesOpamp.setSpec(rbcomponents::diode1S2473());
        feedbackDiodesOpamp.setSeries(1, 2);
        feedbackDiodesOpamp.setSourceR(2600.0f - 1100.0f * d);
        germaniumPair.setSpec(rbcomponents::diodeOA90());
        germaniumPair.setSourceR(1900.0f - 650.0f * d);
        hardClipper.setSpec(rbcomponents::diode1S2473());
        hardClipper.setSourceR(1600.0f - 650.0f * d);
    }

    static inline float siliconFeedbackClip(float x, float d)
    {
        const float pos = 0.63f - 0.08f * d;
        const float neg = 0.84f - 0.10f * d;
        if (x >= 0.0f)
            return pos * std::tanh(x / pos);
        return -neg * std::tanh((-x) / neg);
    }

    static inline float germaniumCrossover(float x, float amount)
    {
        const float dead = 0.010f + 0.024f * amount;
        const float inner = 0.16f + 0.20f * (1.0f - amount);
        if (std::fabs(x) < dead)
            return x * inner;
        return x > 0.0f ? x - dead : x + dead;
    }

    static inline float hardClipD8D9(float x, float d)
    {
        const float threshold = 0.53f - 0.07f * d;
        return threshold * std::tanh(x / threshold);
    }

    float colorBoard(float x)
    {
        const float lo = clamp01(colorLow);
        const float hi = clamp01(colorHigh);
        float low = colorLowCap.process(x);
        low = lowColorBoost.process(low);
        float high = colorHighCap.process(x);
        high = highColorBoost.process(high);
        float presence = colorPresenceCap.process(x);
        presence = highColorShelf.process(presence);
        return x * 0.30f
             + low * (0.14f + 1.10f * lo)
             + high * (0.10f + 1.08f * hi)
             + presence * (0.06f + 0.42f * hi);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        ic1b.setSpec(rbshared::m5218Spec());
        ic2a.setSpec(rbshared::m5218Spec());
        ic1b.setSampleRate(sampleRate);
        ic2a.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q1CouplingC4.reset();
        q4CouplingC6.reset();
        q6CouplingC11.reset();
        opampCouplingC12.reset();
        colorLowCap.reset();
        colorHighCap.reset();
        colorPresenceCap.reset();
        outputCoupling.reset();
        inputFetPole.reset();
        q6FeedbackC10.reset();
        opampFeedbackC9.reset();
        hardClipCapC16.reset();
        outputLoad.reset();
        lowColorBoost.reset();
        highColorBoost.reset();
        highColorShelf.reset();
        postColorLimit.reset();
        q6Dc.reset();
        opampDc.reset();
        geDc.reset();
        ic1b.reset();
        ic2a.reset();
        feedbackDiodesQ6.reset();
        feedbackDiodesOpamp.reset();
        germaniumPair.reset();
        hardClipper.reset();
        updateComponentValues();
    }

    void setDist(float v)
    {
        dist = clamp01(v);
        updateComponentValues();
    }

    void setColorLow(float v)
    {
        colorLow = clamp01(v);
        updateComponentValues();
    }

    void setColorHigh(float v)
    {
        colorHigh = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = smoothstep(dist);

        float x = inputC1.process(0.96f * in);
        x = inputFetPole.process(x);
        x = q1CouplingC4.process(std::tanh(1.03f * x));
        x = q4CouplingC6.process(x);

        const float q6Fb = q6FeedbackC10.process(x);
        float y = (x - 0.22f * q6Fb) * (2.0f + 8.5f * dist + 18.0f * d);
        y = ic1b.process(y, 2.0f + 10.0f * d);
        y = feedbackDiodesQ6.process(y);
        y = q6Dc.process(y);
        y = q6CouplingC11.process(y);

        const float opFb = opampFeedbackC9.process(y);
        y = (y - 0.18f * opFb) * (1.9f + 9.5f * dist + 19.0f * d);
        y = ic2a.process(y, 2.0f + 12.0f * d);
        y = feedbackDiodesOpamp.process(y + 0.010f * dist);
        y = opampDc.process(y);
        y = opampCouplingC12.process(y);

        y = germaniumCrossover(y, 0.45f + 0.55f * d);
        y = 0.82f * y + 0.18f * germaniumPair.process(1.6f * y);
        y = geDc.process(y);
        y = hardClipper.process(1.85f * y);
        y = hardClipCapC16.process(y);

        y = colorBoard(y);
        y = postColorLimit.process(y);
        y = outputLoad.process(outputCoupling.process(y));

        const float trim = 0.68f / (1.0f + 0.32f * dist + 0.24f * d + 0.18f * colorLow + 0.10f * colorHigh);
        return std::tanh(1.02f * y * trim);
    }
};

} // namespace alloydistortion

#endif // ALLOY_DISTORTION_CORE_H
