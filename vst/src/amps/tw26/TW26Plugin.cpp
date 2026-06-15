/*
 * TW26 - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed) amp for the game's Amp_TW26.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in TW26Core.h (plain C++,
 * offline-testable); see that header for the circuit topology + schematic ref.
 *
 * STEREO I/O, single mono core: the amp IS a mono device, so it runs ONE TW26Core
 * (half the CPU of a dual-core stereo build), presents 2-in/2-out, and writes the
 * same processed signal to BOTH channels. Slopsmith's engine routes a 1-out (mono)
 * plugin to a single side -> imbalanced; dual-mono output keeps it centered.
 */
#include "DistrhoPlugin.hpp"
#include "TW26Params.h"
#include "TW26Core.h"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class TW26Plugin : public Plugin
{
    tw26::TW26Core core;
    float params[kParamCount];

    void applyAll()
    {
        core.setTone(params[kTone]);
        core.setInstVol(params[kInstVol]);
        core.setMicVol(params[kMicVol]);
        core.setBright(params[kBright]);
        core.setBass(params[kBass]);
        core.setPresence(params[kPresence]);
    }

public:
    TW26Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW26Def[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TW26"; }
    const char* getDescription() const override { return "Bender Deluxe / Fender 57 Deluxe (5E3) style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'w', '2', '6'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTW26Names[index];
        parameter.symbol = kTW26Symbols[index];
        parameter.ranges.min = kTW26Min[index];
        parameter.ranges.max = kTW26Max[index];
        parameter.ranges.def = kTW26Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = tw26::clamp01(value);
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
            const float y = rbAmpLvl(0.522f * core.process(3.2f * in0[i]));
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW26Plugin)
};

Plugin* createPlugin()
{
    return new TW26Plugin();
}

END_NAMESPACE_DISTRHO
