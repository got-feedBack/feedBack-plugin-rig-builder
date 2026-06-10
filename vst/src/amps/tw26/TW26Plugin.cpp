/*
 * TW26 - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed) amp for Rocksmith's Amp_TW26.
 *
 * DPF/VST3 wrapper. All the DSP lives in TW26Core.h (plain C++, offline-testable);
 * see that header for the circuit topology and the schematic reference.
 */
#include "DistrhoPlugin.hpp"
#include "TW26Params.h"
#include "TW26Core.h"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.30 RMS at real settings); the soft
// knee is transparent below +/-0.80 and saturates to a +/-0.98 ceiling so EQ
// boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class TW26Plugin : public Plugin
{
    tw26::TW26Core left;
    tw26::TW26Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTone(params[kTone]);       right.setTone(params[kTone]);
        left.setInstVol(params[kInstVol]); right.setInstVol(params[kInstVol]);
        left.setMicVol(params[kMicVol]);   right.setMicVol(params[kMicVol]);
        left.setBright(params[kBright]);   right.setBright(params[kBright]);
        left.setBass(params[kBass]);       right.setBass(params[kBass]);
        left.setPresence(params[kPresence]); right.setPresence(params[kPresence]);
    }

public:
    TW26Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW26Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
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
            outL[i] = rbAmpLvl(0.522f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.522f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW26Plugin)
};

Plugin* createPlugin()
{
    return new TW26Plugin();
}

END_NAMESPACE_DISTRHO
