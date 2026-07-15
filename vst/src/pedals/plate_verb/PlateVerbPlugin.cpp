/*
 * PlateVerb - EMT 140-style plate reverb for the game Pedal_PlateVerb.
 *
 * Local reference: pedals/plate reverb.jpg. The useful the game contract is
 * Time, Depth, Mix, and Voice, so this uses the shared dense reverb tank with a
 * shorter, brighter plate voicing and a binary normal/bright Voice switch.
 */
#include "DistrhoPlugin.hpp"
#include "PlateVerbParams.h"
#include "../../_shared/reverb_core.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

} // namespace

class PlateVerbPlugin : public Plugin
{
    ReverbCore reverb;
    float params[kParamCount];

    void applyAll()
    {
        const float time = smoothstep(params[kTime]);
        const float depth = smoothstep(params[kDepth]);
        const float mix = clamp01(params[kMix]);
        const float voice = params[kVoice] >= 0.5f ? 1.0f : 0.0f;

        // Depth = plate SIZE/bloom (was a near-dead knob): a tight, bright small
        // plate -> a big, dense, longer-blooming plate. A real EMT 140 has no LFO,
        // so Depth is size + density, not modulation. Voice adds a bright/large
        // tilt. Higher diffusion + lighter damping = a dense, bright PLATE rather
        // than the previous generic hall wash.
        const float sizeScale  = 0.50f + 0.40f * depth + 0.07f * voice;
        const float dampBias   = (voice > 0.5f ? -0.05f : 0.02f);
        const float apFeedback = 0.66f + 0.08f * depth + 0.04f * voice;
        const float tone       = voice > 0.5f ? 0.60f : 0.50f;

        reverb.setVoicing(sizeScale, dampBias, apFeedback);
        // Time is the main decay; a bigger plate (Depth) blooms a little longer.
        // Modulation depth is 0 (no LFO on a real plate).
        reverb.setParams(0.16f + 0.74f * time + 0.10f * depth,
                         tone,
                         0.0f,
                         0.0f);

        // Reference Mix points, normalized to the full-wet render:
        // 20% = 0.218, 50% = 0.387, 100% = 1.0. The dry plate return stays
        // nearly unchanged through the first half, then reaches true wet-only.
        const float m2 = mix * mix;
        const float wetCurve = clamp01(1.4888333f * mix - 2.3705f * m2
            + 1.8816667f * m2 * mix);
        const float dryCurve = std::cos(1.5707963f * std::pow(mix, 2.8f));
        reverb.setEarlyMix(wetCurve * 2.00f);
        reverb.setOutputMix(dryCurve, wetCurve * 0.50f);
    }

public:
    PlateVerbPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPlateVerbDef[i];
        reverb.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "PlateVerb"; }
    const char* getDescription() const override { return "EMT 140 style plate reverb"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPlateVerbNames[index];
        parameter.symbol = kPlateVerbSymbols[index];
        parameter.ranges.min = kPlateVerbMin[index];
        parameter.ranges.max = kPlateVerbMax[index];
        parameter.ranges.def = kPlateVerbDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        reverb.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
            reverb.process(inL[i], inR[i], outL[i], outR[i]);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlateVerbPlugin)
};

Plugin* createPlugin()
{
    return new PlateVerbPlugin();
}

END_NAMESPACE_DISTRHO
