/*
 * BENDER BASSMAN - Fender Bassman 5F6-A tweed for the game's Amp_TW40.
 * Parody brand "Bender" (same as the SuperNova 22 / Deluxe); the in-app face
 * must never read "Fender".
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in TW40Core.h (plain C++,
 * offline-testable); see that header for the circuit topology + schematic ref
 * (docs/schematics/tw40.md).
 *
 * STEREO I/O, single mono core: the amp IS a mono device, so it runs ONE
 * TW40Core (half the CPU of a dual-core stereo build), presents 2-in/2-out, and
 * writes the same processed signal to BOTH channels. Slopsmith's engine routes
 * a 1-out (mono) plugin to a single side -> imbalanced; dual-mono output keeps
 * it centered. (Matches en30/tw26.)
 *
 * the game: the 5F6-A has no gain knob, so RS Gain -> Bright Volume (the drive
 * into breakup); Treble/Bass/Mid -> tone stack, Pres -> Presence. See
 * rs_knob_to_vst_param.json (input pinned to Both/jumpered for songs).
 */
#include "DistrhoPlugin.hpp"
#include "TW40Params.h"
#include "TW40Core.h"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class TW40Plugin : public Plugin
{
    tw40::TW40Core core;
    float params[kParamCount];

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
            core.setParam(i, params[i]);
    }

public:
    TW40Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW40Def[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TW40"; }
    const char* getDescription() const override { return "Fender Bassman 5F6-A tweed style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'w', '4', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTW40Names[index];
        parameter.symbol = kTW40Symbols[index];
        parameter.ranges.min = kTW40Min[index];
        parameter.ranges.max = kTW40Max[index];
        parameter.ranges.def = kTW40Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = tw40::clamp01(value);
        core.setParam((int)index, params[index]);
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
            const float y = rbAmpLvl(0.600f * core.process(3.2f * in0[i]));
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW40Plugin)
};

Plugin* createPlugin()
{
    return new TW40Plugin();
}

END_NAMESPACE_DISTRHO
