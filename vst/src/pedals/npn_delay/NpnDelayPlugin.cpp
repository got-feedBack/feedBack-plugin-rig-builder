/*
 * NpnDelay - Boss DM-2 style analog delay.
 *
 * Reference: pedals/classic npn delay.pdf. Real controls are Repeat Rate,
 * Echo, and Intensity. The delay path models the NE570 compander, MN3005/
 * MN3205 BBD loss, MN3101/MN3102 clock behaviour, 4558 filtering, and NPN/JFET
 * buffer/switch coloration.
 */
#include "DistrhoPlugin.hpp"
#include "NpnDelayParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class NpnDelayPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kRepeatRate];
        c.mix = params[kEcho];
        c.feedback = params[kIntensity];
        c.drive = 0.34f;
        c.output = 0.66f;
        c.tone = 0.45f;
        c.rate = 0.05f;
        c.depth = 0.10f;
        core.setControls(c);
    }

public:
    NpnDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kNpnDelayDef[i];
        core.setVoice(rbdelay::dm2Voice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NpnDelay"; }
    const char* getDescription() const override { return "Boss DM-2 component-guided BBD delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'p', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kNpnDelayNames[index];
        parameter.symbol = kNpnDelaySymbols[index];
        parameter.ranges.min = kNpnDelayMin[index];
        parameter.ranges.max = kNpnDelayMax[index];
        parameter.ranges.def = kNpnDelayDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NpnDelayPlugin)
};

Plugin* createPlugin()
{
    return new NpnDelayPlugin();
}

END_NAMESPACE_DISTRHO
