#ifndef RB_SHARED_OPAMP_HPP
#define RB_SHARED_OPAMP_HPP

#include <cmath>

namespace rbshared {

struct OpAmpSpec
{
    float gbwHz;
    float slewVPerUs;
    float posSwingV;
    float negSwingV;
    float voltsPerUnit;
};

static inline OpAmpSpec tl072Spec()
{
    return { 3000000.0f, 13.0f, 3.1f, 3.1f, 3.2f };
}

static inline OpAmpSpec tl082Spec()
{
    return { 3000000.0f, 13.0f, 3.0f, 3.0f, 3.2f };
}

static inline OpAmpSpec tlc2264Spec()
{
    return { 710000.0f, 0.55f, 3.7f, 3.7f, 3.2f };
}

static inline OpAmpSpec njm4558Spec()
{
    return { 3000000.0f, 1.0f, 2.8f, 2.8f, 3.0f };
}

static inline OpAmpSpec jrc4558Spec()
{
    return { 3000000.0f, 1.0f, 2.7f, 2.7f, 3.0f };
}

static inline OpAmpSpec upc4558Spec()
{
    return { 3000000.0f, 1.0f, 2.8f, 2.8f, 3.0f };
}

static inline OpAmpSpec m5218Spec()
{
    return { 2000000.0f, 1.0f, 2.8f, 2.8f, 3.0f };
}

static inline OpAmpSpec m5218alSpec()
{
    return { 2000000.0f, 1.0f, 2.8f, 2.8f, 3.0f };
}

static inline OpAmpSpec lm741Spec()
{
    return { 1000000.0f, 0.5f, 2.5f, 2.5f, 3.0f };
}

static inline OpAmpSpec lm308Spec()
{
    return { 800000.0f, 0.30f, 2.5f, 2.5f, 3.0f };
}

static inline OpAmpSpec ta7136apSpec()
{
    return { 900000.0f, 0.45f, 2.2f, 2.2f, 3.0f };
}

static inline OpAmpSpec njm022bSpec()
{
    return { 1000000.0f, 1.0f, 2.45f, 2.45f, 3.0f };
}

static inline OpAmpSpec m5223Spec()
{
    return { 1000000.0f, 0.9f, 2.35f, 2.35f, 3.0f };
}

class OpAmpStage
{
    OpAmpSpec spec = tl072Spec();
    float sampleRate = 48000.0f;
    float y = 0.0f;

    static inline float clampFreq(float hz, float sr)
    {
        const float nyquist = sr * 0.45f;
        return hz < 20.0f ? 20.0f : (hz > nyquist ? nyquist : hz);
    }

public:
    void setSpec(const OpAmpSpec& s)
    {
        spec = s;
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float target, float closedLoopGain)
    {
        const float pi = 3.14159265358979323846f;
        const float gain = closedLoopGain < 1.0f ? 1.0f : closedLoopGain;
        const float pole = clampFreq(spec.gbwHz / gain, sampleRate);
        const float a = 1.0f - std::exp(-2.0f * pi * pole / sampleRate);

        float next = y + a * (target - y);

        const float maxStep = (spec.slewVPerUs * 1000000.0f / sampleRate) / spec.voltsPerUnit;
        const float delta = next - y;
        if (delta > maxStep)
            next = y + maxStep;
        else if (delta < -maxStep)
            next = y - maxStep;

        const float pos = spec.posSwingV / spec.voltsPerUnit;
        const float neg = spec.negSwingV / spec.voltsPerUnit;
        y = next >= 0.0f ? pos * std::tanh(next / pos)
                         : -neg * std::tanh((-next) / neg);
        return y;
    }
};

} // namespace rbshared

#endif // RB_SHARED_OPAMP_HPP
