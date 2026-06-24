/*
 * SBR Super Redhead — SWR Super Redhead (hybrid: 12AX7 tube preamp + op-amp EQ +
 * ~350W solid-state power).
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in RedheadCore.h (built on the shared
 * tube_stage.hpp framework: real 12AX7 TubeStages, the SWR Aural Enhancer contour,
 * the semi-parametric EQ, and a clean SS power stage). Schematic: "Super Redhead
 * (Complete)" — preamp #170007 + SWR2000 power module (±77V BJT).
 *
 * STEREO I/O, single mono core: runs ONE RedheadCore, dual-mono. The nonlinear chain
 * (tubes + SS clip) runs at 2× oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "RedheadParams.h"
#include "RedheadCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO


static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kRedheadMakeup = 0.45f;

class RedheadPlugin : public Plugin
{
    redhead::RedheadCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setGain(fParams[kGain]);
        core.setAural(fParams[kAural]);
        core.setBass(fParams[kBass]);
        core.setMidLevel(fParams[kMidLevel]);
        core.setMidFreq(fParams[kMidFreq]);
        core.setTreble(fParams[kTreble]);
        core.setMaster(fParams[kMaster]);
        core.setActive(fParams[kActive] > 0.5f);
        core.setTurbo(fParams[kTurbo] > 0.5f);
        core.setTransparency(fParams[kTransparency] > 0.5f);
    }

public:
    RedheadPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRedheadDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "SbrRedhead"; }
    const char* getDescription() const override { return "SWR Super Redhead — circuit-real hybrid bass head (12AX7 + SS power)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'R', 'H'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kRedheadNames[i]; p.symbol = kRedheadSymbols[i];
        p.ranges.min = kRedheadMin[i]; p.ranges.max = kRedheadMax[i]; p.ranges.def = kRedheadDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; applyAll(); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) {
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k = 0; k < kOS; ++k)
                ub[k] = rbAmpLvl(kRedheadMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RedheadPlugin)
};

Plugin* createPlugin() { return new RedheadPlugin(); }

END_NAMESPACE_DISTRHO
