/*
 * ModDelay - Ibanez DLL10/DML10 style modulated digital delay.
 *
 * Reference: pedals/moddelay_1.gif and moddelay_2.gif. Real controls are
 * Delay Time, Regen, Delay Level, Speed, and Width. The model follows the
 * M5218/TL062 analog conditioning, NE571/NJM570 companding, MC4101/uPD4164
 * digital delay section, and modulated VCO control.
 */
#include "DistrhoPlugin.hpp"
#include "ModDelayParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class ModDelayPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kDelayTime];
        c.feedback = params[kRegen];
        c.mix = params[kDelayLevel];
        c.rate = params[kSpeed];
        c.depth = params[kWidth];
        c.drive = 0.34f;
        c.output = 0.66f;
        c.tone = 0.56f;
        core.setControls(c);
    }

public:
    ModDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kModDelayDef[i];
        core.setVoice(rbdelay::dll10Voice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ModDelay"; }
    const char* getDescription() const override { return "Ibanez DLL10/DML10 style modulated delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'd', 'D', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kModDelayNames[index];
        parameter.symbol = kModDelaySymbols[index];
        parameter.ranges.min = kModDelayMin[index];
        parameter.ranges.max = kModDelayMax[index];
        parameter.ranges.def = kModDelayDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModDelayPlugin)
};

Plugin* createPlugin()
{
    return new ModDelayPlugin();
}

END_NAMESPACE_DISTRHO
