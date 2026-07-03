/*
 * AnalogDelay - FM104 / Moog MF-104 style BBD delay.
 *
 * Reference: pedals/analog delay.pdf. The model follows the MF-104 topology:
 * TL072 input conditioning, Drive pot, SA572 compander, MN3005/MN3008 BBD
 * delay path, LM13700 wet/dry VCAs, dark feedback filtering, and Output level.
 */
#include "DistrhoPlugin.hpp"
#include "AnalogDelayParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class AnalogDelayPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.drive = params[kDrive];
        c.output = params[kOutput];
        c.delay = params[kTime];
        c.feedback = params[kFeedback];
        c.mix = params[kMix];
        c.tone = 0.55f;
        c.rate = 0.08f;
        c.depth = 0.18f;
        core.setControls(c);
    }

public:
    AnalogDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAnalogDelayDef[i];
        core.setVoice(rbdelay::fm104Voice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AnalogDelay"; }
    const char* getDescription() const override { return "FM104 / MF-104 component-guided BBD delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'n', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAnalogDelayNames[index];
        parameter.symbol = kAnalogDelaySymbols[index];
        parameter.ranges.min = kAnalogDelayMin[index];
        parameter.ranges.max = kAnalogDelayMax[index];
        parameter.ranges.def = kAnalogDelayDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogDelayPlugin)
};

Plugin* createPlugin()
{
    return new AnalogDelayPlugin();
}

END_NAMESPACE_DISTRHO
