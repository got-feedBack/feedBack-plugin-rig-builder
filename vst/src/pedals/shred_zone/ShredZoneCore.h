#ifndef SHRED_ZONE_CORE_H
#define SHRED_ZONE_CORE_H

#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace shredzone {

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

    void setHighPass(float sr, float hz, float q)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        hz = clampFreq(hz, s);
        const float w0 = 2.0f * kPi * hz / s;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
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
};

class ShredZoneCore
{
    // Boss MT-2 schematic anchors from pedals/shred zone.pdf:
    // C042 47 nF/R058 1 M input, dual-gain transistor/op-amp path, D003-D005
    // 1SS133 clipping, VR01 250 k Dist, VR03a/b 100 k Low/High, VR02b
    // 100 k Middle, VR02a 50 k dual Mid Freq, and VR04 50 k Level.
    static constexpr float kC042 = 47.0e-9f;
    static constexpr float kR058 = 1000000.0f;
    static constexpr float kC034 = 27.0e-9f;
    static constexpr float kC035 = 10.0e-9f;
    static constexpr float kR044 = 220000.0f;
    static constexpr float kC032 = 100.0e-12f;
    static constexpr float kC029 = 33.0e-9f;
    static constexpr float kR045 = 10000.0f;
    static constexpr float kC028 = 47.0e-12f;
    static constexpr float kC024 = 15.0e-9f;
    static constexpr float kDistPot = 250000.0f;
    static constexpr float kTonePot = 100000.0f;
    static constexpr float kMidFreqPot = 50000.0f;

    float sampleRate = 48000.0f;
    float dist = 0.70f;
    float low = 0.50f;
    float high = 0.50f;
    float middle = 0.50f;
    float middleFreq = 0.48f;

    RcHighPass inputC042;
    RcHighPass transistorInputC034;
    RcHighPass transistorEmitterC035;
    RcHighPass opampCouplingC029;
    RcHighPass hardClipCouplingC024;
    RcLowPass inputFetPole;
    RcLowPass firstFeedbackC032;
    RcLowPass secondFeedbackC028;
    RcLowPass clipRollOff;
    Biquad preLowTight;
    Biquad preMidPush;
    Biquad lowShelf;
    Biquad middleEq;
    Biquad highShelf;
    Biquad biteEq;
    Biquad outputLowPass;
    DcBlock firstDc;
    DcBlock secondDc;
    DcBlock postClipDc;
    rbshared::OpAmpStage ic2a;
    rbshared::OpAmpStage ic2b;
    rbshared::OpAmpStage ic1a;
    rbcomponents::AsymDiodeStringClipper preampD005;
    rbcomponents::AntiParallelDiodePair hardClipD003D004;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    static inline float siliconClip(float x, float threshold)
    {
        return threshold * std::tanh(x / threshold);
    }

    static inline float midFreqHz(float v)
    {
        // MT-2 panel range is marked roughly 200 Hz to 5 kHz.
        const float t = std::pow(clamp01(v), 1.18f);
        return 200.0f * std::pow(25.0f, t);
    }

    void updateComponentValues()
    {
        const float d = smoothstep(dist);
        const float midHz = midFreqHz(middleFreq);

        inputC042.setRC(sampleRate, kR058, kC042);
        transistorInputC034.setRC(sampleRate, 47000.0f + (1.0f - d) * 0.22f * kDistPot, kC034);
        transistorEmitterC035.setRC(sampleRate, 10000.0f + (1.0f - d) * 0.16f * kDistPot, kC035);
        opampCouplingC029.setRC(sampleRate, kR045 + (1.0f - d) * 0.24f * kDistPot, kC029);
        hardClipCouplingC024.setRC(sampleRate, 12000.0f + (1.0f - d) * 0.10f * kDistPot, kC024);

        inputFetPole.setHz(sampleRate, 15000.0f);
        firstFeedbackC032.setRC(sampleRate, kR044 + d * kDistPot, kC032);
        secondFeedbackC028.setRC(sampleRate, 47000.0f + d * kDistPot, kC028);
        clipRollOff.setHz(sampleRate, 6100.0f - 2200.0f * d + 1200.0f * high);

        preLowTight.setHighPass(sampleRate, 96.0f + 130.0f * d, 0.72f);
        preMidPush.setPeaking(sampleRate, 850.0f + 420.0f * dist, 0.78f, 2.4f + 5.2f * d);

        lowShelf.setLowShelf(sampleRate, 105.0f + 30.0f * low, 0.74f, eqDb(low, 15.0f));
        middleEq.setPeaking(sampleRate, midHz, 0.58f + 0.22f * (1.0f - std::fabs(middle - 0.5f)),
                            eqDb(middle, 15.0f));
        highShelf.setHighShelf(sampleRate, 2500.0f + 1450.0f * high, 0.72f, eqDb(high, 15.0f));
        biteEq.setPeaking(sampleRate, 2100.0f + 850.0f * high, 0.66f, 1.4f + 3.5f * high + 1.0f * d);
        outputLowPass.setLowPass(sampleRate, 3800.0f + 7700.0f * high, 0.62f);

        preampD005.setSpec(rbcomponents::diode1SS133());
        preampD005.setSeries(1, 2);
        preampD005.setSourceR(10000.0f); // R054/Q009 stage Thevenin path
        hardClipD003D004.setSpec(rbcomponents::diode1SS133());
        hardClipD003D004.setSourceR(2200.0f); // R033
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        ic2a.setSpec(rbshared::m5218alSpec());
        ic2b.setSpec(rbshared::m5218alSpec());
        ic1a.setSpec(rbshared::m5218alSpec());
        ic2a.setSampleRate(sampleRate);
        ic2b.setSampleRate(sampleRate);
        ic1a.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputC042.reset();
        transistorInputC034.reset();
        transistorEmitterC035.reset();
        opampCouplingC029.reset();
        hardClipCouplingC024.reset();
        inputFetPole.reset();
        firstFeedbackC032.reset();
        secondFeedbackC028.reset();
        clipRollOff.reset();
        preLowTight.reset();
        preMidPush.reset();
        lowShelf.reset();
        middleEq.reset();
        highShelf.reset();
        biteEq.reset();
        outputLowPass.reset();
        firstDc.reset();
        secondDc.reset();
        postClipDc.reset();
        ic2a.reset();
        ic2b.reset();
        ic1a.reset();
        preampD005.reset();
        hardClipD003D004.reset();
        updateComponentValues();
    }

    void setDist(float v)
    {
        dist = clamp01(v);
        updateComponentValues();
    }

    void setLow(float v)
    {
        low = clamp01(v);
        updateComponentValues();
    }

    void setHigh(float v)
    {
        high = clamp01(v);
        updateComponentValues();
    }

    void setMiddle(float v)
    {
        middle = clamp01(v);
        updateComponentValues();
    }

    void setMiddleFreq(float v)
    {
        middleFreq = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float d = smoothstep(dist);

        float x = inputC042.process(0.96f * in);
        x = inputFetPole.process(x);
        x = preLowTight.process(x);
        x = preMidPush.process(x);

        float y = transistorInputC034.process(x);
        const float emitter = transistorEmitterC035.process(y);
        y = (y + 0.22f * emitter) * (1.85f + 7.5f * dist + 15.5f * d);
        // Q010/Q009 and D005 form the asymmetric discrete pre-distortion
        // stage. D005 is not the later D003/D004 shunt pair.
        y = preampD005.process(2.4f * y) / 2.4f;
        y = firstDc.process(y);

        const float fb1 = firstFeedbackC032.process(y);
        y = (y - 0.18f * fb1) * (1.55f + 5.5f * dist + 12.0f * d);
        y = ic2a.process(y, 2.0f + 12.0f * d);
        y = opampCouplingC029.process(y);

        const float fb2 = secondFeedbackC028.process(y);
        y = (y - 0.15f * fb2 + 0.05f * x) * (1.30f + 4.8f * dist + 11.0f * d);
        y = ic2b.process(y, 2.0f + 10.0f * d);
        y = secondDc.process(y);

        // C027/R033 feed the sole antiparallel audio hard clipper D003/D004.
        // D001/D002 belong to the electronic bypass/output switching and must
        // not create another distortion stage.
        y = hardClipCouplingC024.process(y);
        y = hardClipD003D004.process(y);
        y = clipRollOff.process(y);
        y = postClipDc.process(y);

        y = lowShelf.process(y);
        y = middleEq.process(y);
        y = biteEq.process(y);
        y = highShelf.process(y);
        y = outputLowPass.process(y);

        const float eqEnergy = 1.0f
            + 0.018f * std::fabs((low - 0.5f) * 30.0f)
            + 0.017f * std::fabs((middle - 0.5f) * 30.0f)
            + 0.017f * std::fabs((high - 0.5f) * 30.0f);
        // Once D003/D004 conduct, increasing Dist changes compression and
        // harmonic density rather than making the pedal progressively quieter.
        // Compensate the measured loss of the cascaded RC stages, not peaks.
        const float driveMakeup = 1.0f + 0.55f * d * d;
        const float trim = 0.58f * driveMakeup / eqEnergy;
        return dn(ic1a.process(y * trim, 2.0f));
    }
};

} // namespace shredzone

#endif // SHRED_ZONE_CORE_H
