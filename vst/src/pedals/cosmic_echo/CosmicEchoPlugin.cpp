/*
 * CosmicEcho - Synthrotek ECHO / PT2399 style delay.
 *
 * Reference: pedals/cosmic echo.png plus PT2399 reference circuit. The model
 * follows the PT2399 core, TL074 CV control, feedback tone shaping, and op-amp
 * dry/wet mixer.
 */
#include "DistrhoPlugin.hpp"
#include "CosmicEchoParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class CosmicEchoPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kDelayLength];
        c.feedback = params[kFeedback];
        c.mix = params[kMix];
        c.drive = 0.42f;
        c.output = 0.66f;
        c.tone = 0.50f;
        c.rate = 0.04f;
        c.depth = 0.12f;
        core.setControls(c);
    }

public:
    CosmicEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kCosmicEchoDef[i];
        core.setVoice(rbdelay::pt2399Voice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CosmicEcho"; }
    const char* getDescription() const override { return "Synthrotek ECHO PT2399 delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 's', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kCosmicEchoNames[index];
        parameter.symbol = kCosmicEchoSymbols[index];
        parameter.ranges.min = kCosmicEchoMin[index];
        parameter.ranges.max = kCosmicEchoMax[index];
        parameter.ranges.def = kCosmicEchoDef[index];
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
        core.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbdelay::StereoOut y = core.process(inputs[0][i], inputs[1][i]);
            outputs[0][i] = y.left;
            outputs[1][i] = y.right;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CosmicEchoPlugin)
};

Plugin* createPlugin()
{
    return new CosmicEchoPlugin();
}

END_NAMESPACE_DISTRHO
