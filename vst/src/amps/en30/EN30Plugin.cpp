/*
 * BOX DC30 - AC30 Top Boost-style amp for Rocksmith's Amp_EN30.
 *
 * DPF/VST3 wrapper. All the DSP lives in EN30Core.h (plain C++, offline-testable);
 * see that header for the circuit topology and schematic references.
 */
#include "DistrhoPlugin.hpp"
#include "EN30Params.h"
#include "EN30Core.h"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.30 RMS at real settings); the soft
// knee is transparent below +/-0.80 and saturates to a +/-0.98 ceiling so EQ
// boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.80f,c=0.98f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class EN30Plugin : public Plugin
{
    en30::EN30Core left;
    en30::EN30Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setNormalVol(params[kNormalVol]); right.setNormalVol(params[kNormalVol]);
        left.setTBVol(params[kTBVol]);         right.setTBVol(params[kTBVol]);
        left.setTreble(params[kTreble]);       right.setTreble(params[kTreble]);
        left.setBass(params[kBass]);           right.setBass(params[kBass]);
        left.setRevTone(params[kRevTone]);     right.setRevTone(params[kRevTone]);
        left.setRevLevel(params[kRevLevel]);   right.setRevLevel(params[kRevLevel]);
        left.setSpeed(params[kSpeed]);         right.setSpeed(params[kSpeed]);
        left.setDepth(params[kDepth]);         right.setDepth(params[kDepth]);
        left.setCut(params[kCut]);             right.setCut(params[kCut]);
        left.setMaster(params[kMaster]);       right.setMaster(params[kMaster]);
        left.setInput(params[kInput]);         right.setInput(params[kInput]);
        left.setBright(params[kBright]);       right.setBright(params[kBright]);
    }

public:
    EN30Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEN30Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BOX DC30"; }
    const char* getDescription() const override { return "BOX DC30 / AC30 Top Boost style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', 'n', '3', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEN30Names[index];
        parameter.symbol = kEN30Symbols[index];
        parameter.ranges.min = kEN30Min[index];
        parameter.ranges.max = kEN30Max[index];
        parameter.ranges.def = kEN30Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = en30::clamp01(value);
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
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            outL[i] = rbAmpLvl(0.767f * left.process(inL[i]));
            outR[i] = rbAmpLvl(0.767f * right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EN30Plugin)
};

Plugin* createPlugin()
{
    return new EN30Plugin();
}

END_NAMESPACE_DISTRHO
