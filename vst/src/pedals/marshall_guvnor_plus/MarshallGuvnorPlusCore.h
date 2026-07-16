#ifndef MARSHALL_GUVNOR_PLUS_CORE_H
#define MARSHALL_GUVNOR_PLUS_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace marshallguvnorplus {

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

class AnalogIir1
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double x1 = 0.0, y1 = 0.0;
public:
    void setAnalog(float sr, double n1, double n0, double d1, double d0)
    {
        const double k = 2.0 * static_cast<double>(sr);
        const double denominator = d1 * k + d0;
        b0 = (n1 * k + n0) / denominator;
        b1 = (-n1 * k + n0) / denominator;
        a1 = (-d1 * k + d0) / denominator;
    }
    void reset() { x1 = y1 = 0.0; }
    float process(float input)
    {
        const double x = input;
        double y = b0*x + b1*x1 - a1*y1;
        x1 = x;
        y1 = y;
        if (!std::isfinite(y))
        {
            reset();
            y = 0.0;
        }
        return dn(static_cast<float>(y));
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

    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        const float srate = sr > 1000.0f ? sr : 48000.0f;
        hz = clampFreq(hz, srate);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / srate;
        const float c = std::cos(w0);
        const float si = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = si * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
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

class MarshallGuvnorPlusCore
{
    // Marshall GV-2/Guv'nor Plus anchors from pedals/Marshall GV2_1.png and
    // pedals/marshall gv2_2.gif: TL072 gain stages, Gain/Treble 100 kB,
    // Bass/Mid 100 kA, Deep/Volume 1 MB, 3 mm red LED clipping, D3/D4 1N4148
    // shaping, and the passive/active tone network around C12/C13/C14.
    static constexpr float kInputC = 9.6e-9f;
    static constexpr float kInputR = 1000000.0f;
    static constexpr float kGainPot = 100000.0f;
    static constexpr float kFeedbackC = 120.0e-12f;
    static constexpr float kCouplingC = 220.0e-9f;
    static constexpr float kClipFeedbackR = 680000.0f;
    static constexpr float kClipFeedbackC = 220.0e-12f;

    float sampleRate = 48000.0f;
    float gain = 0.45f;
    float bass = 0.52f;
    float mid = 0.56f;
    float treble = 0.54f;
    float deep = 0.38f;
    float firstStageNoiseGain = 1.0f;

    RcHighPass inputC5;
    RcHighPass interstageC3;
    AnalogIir1 firstGainTransfer;
    RcLowPass gainFeedbackC2;
    RcLowPass clipFeedbackC8;
    RcLowPass ledCap;
    Biquad deepShelf;
    Biquad preVoice;
    Biquad bassShelf;
    Biquad midEq;
    Biquad trebleShelf;
    Biquad outputPresence;
    rbshared::OpAmpStage op1a;
    rbshared::OpAmpStage op1b;
    rbcomponents::AntiParallelDiodePair ledClipper;
    DcBlock clipDc;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateComponentValues()
    {
        const float g = clamp01(gain); // VR1 100kB
        const float gainResistance = 100.0f + g * kGainPot;
        firstStageNoiseGain = 1.0f + gainResistance / 2200.0f;
        inputC5.setRC(sampleRate, kInputR, kInputC);
        interstageC3.setRC(sampleRate, 10000.0f, kCouplingC);
        firstGainTransfer.setAnalog(sampleRate,
                                    (2200.0f + gainResistance) * 100.e-9f, 1.0,
                                    2200.0f * 100.e-9f, 1.0);
        gainFeedbackC2.setRC(sampleRate, gainResistance, kFeedbackC);
        clipFeedbackC8.setRC(sampleRate, kClipFeedbackR, kClipFeedbackC);
        ledCap.setHz(sampleRate, 7600.0f + 1000.0f * treble);

        deepShelf.setLowShelf(sampleRate, 86.0f + 55.0f * deep, 0.76f, -2.0f + 10.5f * deep);
        preVoice.setPeaking(sampleRate, 760.0f, 0.74f, 1.2f);
        bassShelf.setLowShelf(sampleRate, 135.0f, 0.74f, eqDb(bass, 11.5f));
        midEq.setPeaking(sampleRate, 700.0f + 460.0f * mid, 0.66f, eqDb(mid, 12.0f));
        trebleShelf.setHighShelf(sampleRate, 2350.0f + 1650.0f * treble, 0.72f, eqDb(treble, 12.5f));
        outputPresence.setPeaking(sampleRate, 2100.0f + 900.0f * treble, 0.72f, 0.7f + 2.5f * treble);
        ledClipper.setSpec(rbcomponents::redLed3mm());
        ledClipper.setSourceR(1000.0f); // R7
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        op1a.setSpec(rbshared::tl072Spec());
        op1b.setSpec(rbshared::tl072Spec());
        op1a.setSampleRate(sampleRate);
        op1b.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC5.reset();
        interstageC3.reset();
        firstGainTransfer.reset();
        gainFeedbackC2.reset();
        clipFeedbackC8.reset();
        ledCap.reset();
        deepShelf.reset();
        preVoice.reset();
        bassShelf.reset();
        midEq.reset();
        trebleShelf.reset();
        outputPresence.reset();
        op1a.reset();
        op1b.reset();
        ledClipper.reset();
        clipDc.reset();
        updateComponentValues();
    }

    void setGain(float v) { gain = clamp01(v); updateComponentValues(); }
    void setBass(float v) { bass = clamp01(v); updateComponentValues(); }
    void setMid(float v) { mid = clamp01(v); updateComponentValues(); }
    void setTreble(float v) { treble = clamp01(v); updateComponentValues(); }
    void setDeep(float v) { deep = clamp01(v); updateComponentValues(); }

    float process(float in)
    {
        float x = inputC5.process(0.96f * in);
        x = deepShelf.process(x);
        x = preVoice.process(x);

        // IC1a: VR1/R1/C1 set the variable non-inverting gain; C2 is the
        // physical feedback compensation capacitor.
        float y = firstGainTransfer.process(x);
        y = op1a.process(gainFeedbackC2.process(y), firstStageNoiseGain);
        y = interstageC3.process(y);

        // IC1b is a fixed inverting 680k/10k stage with C8 in parallel with
        // R6. The 3 mm red LEDs are the only hard shunt clipper.
        y = op1b.process(-68.0f * clipFeedbackC8.process(y), 69.0f);
        y = ledClipper.process(y);
        y = ledCap.process(y);
        y = clipDc.process(y);

        y = bassShelf.process(y);
        y = midEq.process(y);
        y = trebleShelf.process(y);
        y = outputPresence.process(y);

        const float trim = 0.64f / (1.0f + 0.18f * gain + 0.12f * deep);
        return dn(y * trim);
    }
};

} // namespace marshallguvnorplus

#endif // MARSHALL_GUVNOR_PLUS_CORE_H
