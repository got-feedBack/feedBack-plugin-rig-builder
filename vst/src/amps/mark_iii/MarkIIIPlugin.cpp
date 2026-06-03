#include "DistrhoPlugin.hpp"
#include "MarkIIIParams.h"
#include "../mark_shared/MarkAmpCore.h"

START_NAMESPACE_DISTRHO

class MarkIIIPlugin : public Plugin
{
    RbMarkAmpCore left{false};
    RbMarkAmpCore right{false};
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]); right.setGain(params[kGain]);
        left.setBass(params[kBass]); right.setBass(params[kBass]);
        left.setMid(params[kMid]); right.setMid(params[kMid]);
        left.setTreble(params[kTreble]); right.setTreble(params[kTreble]);
    }

public:
    MarkIIIPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMarkIIIDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarkIII"; }
    const char* getDescription() const override { return "Mesa-Boogie Mark III R2 crunch-mode style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'K', '3', 'C'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarkIIINames[index];
        parameter.symbol = kMarkIIISymbols[index];
        parameter.ranges.min = kMarkIIIMin[index];
        parameter.ranges.max = kMarkIIIMax[index];
        parameter.ranges.def = kMarkIIIDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = rbClamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            outputs[0][i] = left.process(inputs[0][i]);
            outputs[1][i] = right.process(inputs[1][i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkIIIPlugin)
};

Plugin* createPlugin()
{
    return new MarkIIIPlugin();
}

END_NAMESPACE_DISTRHO
