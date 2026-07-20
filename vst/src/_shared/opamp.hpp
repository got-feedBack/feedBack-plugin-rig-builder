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

static inline OpAmpSpec tl074aSpec()
{
    return { 3000000.0f, 13.0f, 3.1f, 3.1f, 3.2f };
}

static inline OpAmpSpec tl082Spec()
{
    return { 3000000.0f, 13.0f, 3.0f, 3.0f, 3.2f };
}

static inline OpAmpSpec tl061Spec()
{
    return { 1000000.0f, 3.5f, 2.45f, 2.45f, 3.0f };
}

static inline OpAmpSpec tl064Spec()
{
    return { 1000000.0f, 3.5f, 2.45f, 2.45f, 3.0f };
}

static inline OpAmpSpec tlc2264Spec()
{
    return { 710000.0f, 0.55f, 3.7f, 3.7f, 3.2f };
}

static inline OpAmpSpec tlv2374Spec()
{
    return { 3000000.0f, 2.4f, 3.65f, 3.65f, 3.2f };
}

static inline OpAmpSpec opa1644Spec()
{
    return { 11000000.0f, 20.0f, 4.1f, 4.1f, 3.2f };
}

static inline OpAmpSpec ts921Spec()
{
    return { 4000000.0f, 1.3f, 3.8f, 3.8f, 3.2f };
}

static inline OpAmpSpec ts922Spec()
{
    return { 4000000.0f, 1.3f, 3.8f, 3.8f, 3.2f };
}

static inline OpAmpSpec ts925Spec()
{
    return { 4000000.0f, 1.3f, 3.9f, 3.9f, 3.2f };
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

static inline OpAmpSpec lm741DualRailSpec()
{
    return { 1000000.0f, 0.5f, 13.0f, 13.0f, 3.0f };
}

static inline OpAmpSpec mc1458Spec()
{
    // LM/MC1458 on +/-15 V rails: 1 MHz-class compensated dual op-amp,
    // 0.5 V/us slew and about +/-13 V typical swing into 10 kOhm.
    return { 1000000.0f, 0.5f, 13.0f, 13.0f, 3.0f };
}

static inline OpAmpSpec lm324Spec()
{
    return { 1200000.0f, 0.5f, 2.55f, 2.55f, 3.0f };
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

static inline OpAmpSpec ba718Spec()
{
    return { 1000000.0f, 0.8f, 2.25f, 2.25f, 3.0f };
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

// Op-amp stage with a genuinely linear centre region and a soft transition
// only near the output rails. Use this for bipolar-supply studio/vintage
// circuits where the signal normally remains several volts below clipping;
// OpAmpStage intentionally provides broader saturation for dirt circuits.
class LinearOpAmpStage
{
    OpAmpSpec spec = tl072Spec();
    float sampleRate = 48000.0f;
    float y = 0.0f;

    static inline float clampFreq(float hz, float sr)
    {
        const float nyquist = sr * 0.45f;
        return hz < 20.0f ? 20.0f : (hz > nyquist ? nyquist : hz);
    }

    static inline float softRail(float value, float positive, float negative)
    {
        const float rail = value >= 0.0f ? positive : negative;
        const float sign = value >= 0.0f ? 1.0f : -1.0f;
        const float magnitude = std::fabs(value);
        const float knee = 0.86f * rail;
        if (magnitude <= knee)
            return value;
        const float span = rail - knee;
        return sign * (knee + span * std::tanh((magnitude - knee) / span));
    }

public:
    void setSpec(const OpAmpSpec& value) { spec = value; }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset() { y = 0.0f; }

    float process(float target, float closedLoopGain)
    {
        const float pi = 3.14159265358979323846f;
        const float gain = closedLoopGain < 1.0f ? 1.0f : closedLoopGain;
        const float pole = clampFreq(spec.gbwHz / gain, sampleRate);
        const float a = 1.0f - std::exp(-2.0f * pi * pole / sampleRate);
        float next = y + a * (target - y);

        const float maxStep = (spec.slewVPerUs * 1000000.0f / sampleRate)
                            / spec.voltsPerUnit;
        const float delta = next - y;
        if (delta > maxStep)
            next = y + maxStep;
        else if (delta < -maxStep)
            next = y - maxStep;

        y = softRail(next, spec.posSwingV / spec.voltsPerUnit,
                           spec.negSwingV / spec.voltsPerUnit);
        return y;
    }
};

} // namespace rbshared

#endif // RB_SHARED_OPAMP_HPP
