/*
 * OilCanEcho - Tel-Ray / OK Pacemaker oil-can echo.
 *
 * Reference: pedals/telray_delay_echo_reverb_oilcan_sch.pdf_1.png plus the
 * older local oilcan images. The model follows the discrete transistor preamp,
 * electrostatic rotating storage can, bias oscillator texture, smeared repeats
 * and mixed delay output.
 */
#include "DistrhoPlugin.hpp"
#include "OilCanEchoParams.h"
#include "../../_shared/DelayComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class OilCanEchoPlugin : public Plugin
{
    rbdelay::ComponentDelayCore core;
    float params[kParamCount];

    void applyAll()
    {
        rbdelay::DelayControls c;
        c.delay = params[kTime];
        c.feedback = params[kSustain];
        c.mix = params[kMix];
        c.tone = params[kTone];
        c.drive = 0.48f;
        c.output = 0.66f;
        c.rate = 0.18f;
        c.depth = 0.62f;
        core.setControls(c);
    }

public:
    OilCanEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOilCanEchoDef[i];
        core.setVoice(rbdelay::oilCanVoice());
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "OilCanEcho"; }
    const char* getDescription() const override { return "Tel-Ray style electrostatic oil-can echo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'i', 'E', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOilCanEchoNames[index];
        parameter.symbol = kOilCanEchoSymbols[index];
        parameter.ranges.min = kOilCanEchoMin[index];
        parameter.ranges.max = kOilCanEchoMax[index];
        parameter.ranges.def = kOilCanEchoDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OilCanEchoPlugin)
};

Plugin* createPlugin()
{
    return new OilCanEchoPlugin();
}

END_NAMESPACE_DISTRHO
