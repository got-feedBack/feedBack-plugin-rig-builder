/*
 * NoFiEcho - Ibanez DE7 style Delay/Echo.
 *
 * Reference: pedals/ibanez_de7.pdf_1.png and nofi echo.png. The exact digital
 * IC is proprietary, so the model follows the visible circuit blocks: switched
 * Delay/Echo mode, range switching, digital delay core, analog repeat filters,
 * JFET switching and stereo output buffering.
 */
#include "DistrhoPlugin.hpp"
#include "NoFiEchoParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class NoFiEchoPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kDelayTime];
        c.feedback = params[kRepeat];
        c.mix = params[kDelayLevel];
        c.mode = params[kMode] >= 0.5f ? 1.0f : 0.0f;
        c.range = params[kRange];
        c.drive = 0.30f;
        c.output = 0.66f;
        c.tone = 0.42f + 0.24f * c.mode + 0.10f * c.range;
        c.rate = 0.03f + 0.08f * c.mode;
        c.depth = 0.04f + 0.14f * c.mode;
        core.setControls(c);
    }

public:
    NoFiEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kNoFiEchoDef[i];
        core.setVoice(rbdelay::de7Voice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NoFiEcho"; }
    const char* getDescription() const override { return "Ibanez DE7 Delay/Echo style lo-fi stereo delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'f', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kMode)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kNoFiEchoNames[index];
        parameter.symbol = kNoFiEchoSymbols[index];
        parameter.ranges.min = kNoFiEchoMin[index];
        parameter.ranges.max = kNoFiEchoMax[index];
        parameter.ranges.def = kNoFiEchoDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kMode) ? (value >= 0.5f ? 1.0f : 0.0f)
                                                   : clamp01(value);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoFiEchoPlugin)
};

Plugin* createPlugin()
{
    return new NoFiEchoPlugin();
}

END_NAMESPACE_DISTRHO
