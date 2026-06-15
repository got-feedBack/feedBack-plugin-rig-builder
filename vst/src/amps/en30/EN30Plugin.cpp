/*
 * BOX DC30 - AC30 Top Boost-style amp for the game's Amp_EN30.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in EN30Core.h (plain C++,
 * offline-testable); see that header for the circuit topology and schematic refs.
 *
 * STEREO I/O, single mono core: the amp IS a mono device, so it runs ONE EN30Core
 * (half the CPU of a true dual-core stereo build), but it presents 2-in/2-out and
 * writes the same processed signal to BOTH output channels. Slopsmith's engine
 * routed a 1-out (mono) plugin to a single side -> imbalanced/louder on one side;
 * dual-mono output keeps it centered/balanced while staying CPU-cheap.
 */
#include "DistrhoPlugin.hpp"
#include "EN30Params.h"
#include "EN30Core.h"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class EN30Plugin : public Plugin
{
    en30::EN30Core core;
    float params[kParamCount];

    void applyAll()
    {
        core.setNormalVol(params[kNormalVol]);
        core.setTBVol(params[kTBVol]);
        core.setTreble(params[kTreble]);
        core.setBass(params[kBass]);
        core.setRevTone(params[kRevTone]);
        core.setRevLevel(params[kRevLevel]);
        core.setSpeed(params[kSpeed]);
        core.setDepth(params[kDepth]);
        core.setCut(params[kCut]);
        core.setMaster(params[kMaster]);
        core.setInput(params[kInput]);
        core.setBright(params[kBright]);
    }

public:
    EN30Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEN30Def[i];
        core.setSampleRate((float)getSampleRate());
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
        core.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* in0 = inputs[0];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float y = rbAmpLvl(0.505f * core.process(3.2f * in0[i]));
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EN30Plugin)
};

Plugin* createPlugin()
{
    return new EN30Plugin();
}

END_NAMESPACE_DISTRHO
