/*
 * ValveEcho - Binson Echorec PE603-T style valve drum echo.
 *
 * References: pedals/valveecho_1.png and valveecho_2.png. The model follows
 * ECC83/ECC82 gain stages, OA81 rectified biasing, EM84 metering coloration,
 * magnetic drum smear, four playback head ratios and Swell feedback.
 */
#include "DistrhoPlugin.hpp"
#include "ValveEchoParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class ValveEchoPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kDrumSpeed];
        c.feedback = params[kSwell];
        c.mix = params[kEchoVolume];
        c.tone = params[kTone];
        c.heads = params[kHeads];
        c.drive = 0.46f;
        c.output = 0.66f;
        c.rate = 0.10f;
        c.depth = 0.34f;
        core.setControls(c);
    }

public:
    ValveEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kValveEchoDef[i];
        core.setVoice(rbdelay::binsonVoice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ValveEcho"; }
    const char* getDescription() const override { return "Binson Echorec PE603-T style valve drum echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 'l', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kValveEchoNames[index];
        parameter.symbol = kValveEchoSymbols[index];
        parameter.ranges.min = kValveEchoMin[index];
        parameter.ranges.max = kValveEchoMax[index];
        parameter.ranges.def = kValveEchoDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValveEchoPlugin)
};

Plugin* createPlugin()
{
    return new ValveEchoPlugin();
}

END_NAMESPACE_DISTRHO
