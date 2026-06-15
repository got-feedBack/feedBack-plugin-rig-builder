/*
 * DualRect - Mesa/Boogie 3-Channel Dual Rectifier Solo Head for the game's
 * Amp_CA100. DPF/VST3 wrapper; all DSP is in DualRectCore.h (offline-testable).
 *
 * Reference: amps/Dual Rectifier (Cali_100)/Boogie_3ch_dual_rectifier.pdf
 * Full 3-channel panel (Green/Orange/Red, each Gain/Treble/Mid/Bass/Presence/
 * Master + mode) + Channel select / Output / Rectifier. the game maps 1:1 onto
 * the Red channel (Modern, Bold) via data/rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "DualRectParams.h"
#include "DualRectCore.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness; the soft knee is transparent below
// +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts never hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class DualRectPlugin : public Plugin
{
    dualrect::DualRectCore left;
    dualrect::DualRectCore right;
    float params[kParamCount];

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            left.setParam(i, params[i]);
            right.setParam(i, params[i]);
        }
    }

public:
    DualRectPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDualRectDef[i];
        left.initDefaults();
        right.initDefaults();
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DualRect"; }
    const char* getDescription() const override { return "Mesa-Boogie 3-channel Dual Rectifier style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'R', 'C', 'T'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDualRectNames[index];
        parameter.symbol = kDualRectSymbols[index];
        parameter.ranges.min = kDualRectMin[index];
        parameter.ranges.max = kDualRectMax[index];
        parameter.ranges.def = kDualRectDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = dualrect::clamp01(value);
        left.setParam((int)index, params[index]);
        right.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            outL[i] = rbAmpLvl(0.470f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.470f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DualRectPlugin)
};

Plugin* createPlugin()
{
    return new DualRectPlugin();
}

END_NAMESPACE_DISTRHO
