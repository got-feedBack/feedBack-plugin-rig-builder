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

        const float sizeScale = 0.58f + 0.13f * depth + 0.08f * voice;
        const float dampBias = voice > 0.5f ? -0.08f : 0.015f;
        const float apFeedback = 0.61f + 0.06f * depth + 0.04f * voice;
        const float tone = voice > 0.5f ? 0.78f : 0.56f;

        reverb.setVoicing(sizeScale, dampBias, apFeedback);
        reverb.setParams(0.18f + 0.80f * time,
                         tone,
                         depth * (0.34f + 0.22f * voice),
                         mix * (0.62f + 0.08f * depth));
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
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
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
