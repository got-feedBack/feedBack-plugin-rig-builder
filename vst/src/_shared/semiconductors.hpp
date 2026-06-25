#ifndef RB_SHARED_SEMICONDUCTORS_HPP
#define RB_SHARED_SEMICONDUCTORS_HPP

#include <cmath>

namespace rbcomponents {

static inline float rbClamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float rbDenormal(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

struct DiodeSpec
{
    float isAmp;
    float ideality;
    float maxAbsV;
};

static inline DiodeSpec diode1N4148()
{
    return { 2.52e-9f, 1.75f, 1.20f };
}

static inline DiodeSpec diode1N914()
{
    return { 2.20e-9f, 1.78f, 1.20f };
}

static inline DiodeSpec diode1S2473()
{
    return { 2.60e-9f, 1.78f, 1.20f };
}

static inline DiodeSpec diode1SS133()
{
    return { 2.10e-9f, 1.72f, 1.15f };
}

static inline DiodeSpec diode1SS301()
{
    return { 2.30e-9f, 1.74f, 1.15f };
}

static inline DiodeSpec diode1SS355()
{
    return { 2.45e-9f, 1.75f, 1.20f };
}

static inline DiodeSpec diode1N34A()
{
    return { 2.20e-7f, 1.35f, 0.90f };
}

static inline DiodeSpec diodeOA90()
{
    return { 1.10e-7f, 1.42f, 0.85f };
}

static inline DiodeSpec diode1S1830Rectifier()
{
    return { 8.5e-11f, 2.00f, 1.25f };
}

static inline DiodeSpec diode1N5817()
{
    return { 1.20e-6f, 1.08f, 0.65f };
}

static inline DiodeSpec junctionBC547C()
{
    return { 6.0e-15f, 1.92f, 0.95f };
}

static inline DiodeSpec junction2SC4116GR()
{
    return { 5.8e-15f, 1.92f, 0.95f };
}

static inline DiodeSpec junction2SC3324GR()
{
    return { 7.0e-15f, 1.90f, 0.95f };
}

static inline DiodeSpec junction2N404A()
{
    return { 8.50e-7f, 1.40f, 1.15f };
}

static inline DiodeSpec zenerRD5V6()
{
    return { 2.0e-10f, 2.00f, 5.80f };
}

static inline DiodeSpec redLed3mm()
{
    return { 1.0e-18f, 2.05f, 2.10f };
}

class AntiParallelDiodePair
{
    DiodeSpec spec = diode1N4148();
    float sourceR = 2200.0f;
    float v = 0.0f;

    float nVt() const
    {
        return spec.ideality * 0.02585f;
    }

public:
    void setSpec(const DiodeSpec& s)
    {
        spec = s;
        v = rbClamp(v, -spec.maxAbsV, spec.maxAbsV);
    }

    void setSourceR(float rOhm)
    {
        sourceR = rOhm < 47.0f ? 47.0f : rOhm;
    }

    void reset()
    {
        v = 0.0f;
    }

    float process(float vin)
    {
        const float vt = nVt();
        for (int i = 0; i < 8; ++i)
        {
            const float e = rbClamp(v / vt, -20.0f, 20.0f);
            const float sh = std::sinh(e);
            const float ch = std::cosh(e);
            const float f = (v - vin) / sourceR + 2.0f * spec.isAmp * sh;
            const float fp = 1.0f / sourceR + 2.0f * spec.isAmp * ch / vt;
            v -= f / fp;
            v = rbClamp(v, -spec.maxAbsV, spec.maxAbsV);
        }
        return rbDenormal(v);
    }
};

class AsymDiodeStringClipper
{
    DiodeSpec spec = diode1N4148();
    float sourceR = 2200.0f;
    float v = 0.0f;
    int posSeries = 1;
    int negSeries = 2;

    float branchCurrent(float nodeV, int series) const
    {
        const float vt = spec.ideality * 0.02585f * static_cast<float>(series < 1 ? 1 : series);
        const float e = rbClamp(nodeV / vt, -20.0f, 20.0f);
        return spec.isAmp * (std::exp(e) - 1.0f);
    }

    float branchConductance(float nodeV, int series) const
    {
        const float vt = spec.ideality * 0.02585f * static_cast<float>(series < 1 ? 1 : series);
        const float e = rbClamp(nodeV / vt, -20.0f, 20.0f);
        return spec.isAmp * std::exp(e) / vt;
    }

public:
    void setSpec(const DiodeSpec& s)
    {
        spec = s;
        v = rbClamp(v, -spec.maxAbsV * negSeries, spec.maxAbsV * posSeries);
    }

    void setSourceR(float rOhm)
    {
        sourceR = rOhm < 47.0f ? 47.0f : rOhm;
    }

    void setSeries(int posCount, int negCount)
    {
        posSeries = posCount < 1 ? 1 : posCount;
        negSeries = negCount < 1 ? 1 : negCount;
    }

    void reset()
    {
        v = 0.0f;
    }

    float process(float vin)
    {
        const float posLimit = spec.maxAbsV * static_cast<float>(posSeries);
        const float negLimit = spec.maxAbsV * static_cast<float>(negSeries);
        for (int i = 0; i < 8; ++i)
        {
            const float iPos = branchCurrent(v, posSeries);
            const float iNeg = branchCurrent(-v, negSeries);
            const float gPos = branchConductance(v, posSeries);
            const float gNeg = branchConductance(-v, negSeries);
            const float f = (v - vin) / sourceR + iPos - iNeg;
            const float fp = 1.0f / sourceR + gPos + gNeg;
            v -= f / fp;
            v = rbClamp(v, -negLimit, posLimit);
        }
        return rbDenormal(v);
    }
};

class OcdMosfetGeClipper
{
    DiodeSpec ge = diode1N34A();
    float sourceR = 10000.0f;
    float v = 0.0f;
    float mosfetK = 0.0018f;
    float posVth = 1.55f;
    float negVth = 1.18f;

    static inline float mosCurrent(float nodeV, float vth, float k)
    {
        const float over = nodeV - vth;
        return over > 0.0f ? k * over * over : 0.0f;
    }

    static inline float mosConductance(float nodeV, float vth, float k)
    {
        const float over = nodeV - vth;
        return over > 0.0f ? 2.0f * k * over : 0.0f;
    }

    float geCurrent(float nodeV) const
    {
        const float vt = ge.ideality * 0.02585f;
        const float e = rbClamp(nodeV / vt, -20.0f, 20.0f);
        return ge.isAmp * (std::exp(e) - 1.0f);
    }

    float geConductance(float nodeV) const
    {
        const float vt = ge.ideality * 0.02585f;
        const float e = rbClamp(nodeV / vt, -20.0f, 20.0f);
        return ge.isAmp * std::exp(e) / vt;
    }

public:
    void setSourceR(float rOhm)
    {
        sourceR = rOhm < 100.0f ? 100.0f : rOhm;
    }

    void setHardness(float amount)
    {
        const float a = rbClamp(amount, 0.0f, 1.0f);
        mosfetK = 0.00135f + 0.00115f * a;
        posVth = 1.62f - 0.16f * a;
        negVth = 1.26f - 0.13f * a;
    }

    void reset()
    {
        v = 0.0f;
    }

    float process(float vin)
    {
        for (int i = 0; i < 10; ++i)
        {
            const float iPos = mosCurrent(v, posVth, mosfetK) + 0.55f * geCurrent(v);
            const float iNeg = mosCurrent(-v, negVth, mosfetK);
            const float gPos = mosConductance(v, posVth, mosfetK) + 0.55f * geConductance(v);
            const float gNeg = mosConductance(-v, negVth, mosfetK);
            const float f = (v - vin) / sourceR + iPos - iNeg;
            const float fp = 1.0f / sourceR + gPos + gNeg;
            v -= f / fp;
            v = rbClamp(v, -3.2f, 4.6f);
        }
        return rbDenormal(v);
    }
};

} // namespace rbcomponents

#endif // RB_SHARED_SEMICONDUCTORS_HPP
