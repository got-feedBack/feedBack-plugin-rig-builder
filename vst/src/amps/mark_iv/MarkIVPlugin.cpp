#include "DistrhoPlugin.hpp"
#include "MarkIVParams.h"
#include "../mark_shared/MarkAmpCore.h"

START_NAMESPACE_DISTRHO

class MarkIVPlugin : public Plugin
{
    RbMarkAmpCore left{true};
    RbMarkAmpCore right{true};
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]); right.setGain(params[kGain]);
        left.setBass(params[kBass]); right.setBass(params[kBass]);
        left.setMid(params[kMid]); right.setMid(params[kMid]);
        left.setTreble(params[kTreble]); right.setTreble(params[kTreble]);
    }

public:
    MarkIVPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMarkIVDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarkIV"; }
    const char* getDescription() const override { return "Mesa-Boogie Mark IV lead-channel style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'K', 'I', 'V'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarkIVNames[index];
        parameter.symbol = kMarkIVSymbols[index];
        parameter.ranges.min = kMarkIVMin[index];
        parameter.ranges.max = kMarkIVMax[index];
        parameter.ranges.def = kMarkIVDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkIVPlugin)
};

Plugin* createPlugin()
{
    return new MarkIVPlugin();
}

END_NAMESPACE_DISTRHO
