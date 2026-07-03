/*
 * Dustup CDN — Ashdown ABM500 EVO (Bass Magnifier) bass head.
 *
 * DPF wrapper (VST3 + AU). All DSP lives in DustupCore.h (circuit-real, built on
 * the shared tube_stage.hpp framework: a real ECC83 TubeStage for the Valve Drive
 * + clean solid-state biquads for the Ashdown tone stack, 6-band graphic EQ,
 * sub-harmonic generator and opto comp; SS power amp). Schematic: Ashdown ABM500
 * EVO preamp (APC010).
 *
 * STEREO I/O, single mono core (the amp is a mono device): runs ONE DustupCore and
 * writes it to both outputs (dual-mono). The nonlinear chain runs at 2x oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "DustupParams.h"
#include "DustupCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Family loudness makeup into the soft knee. DustupCore already applies its own
// output/valve-dependent outLevel; this is the final family-level trim.
static constexpr float kDustupMakeup = 0.50f;

class DustupPlugin : public Plugin {
    dustup::DustupCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

public:
    DustupPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kDustupDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        core.setParams(fParams);
    }
protected:
    const char* getLabel()       const override { return "DustupCDN"; }
    const char* getDescription() const override { return "Ashdown ABM EVO Bass Magnifier head — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'D', 'U'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kDustupNames[i]; p.symbol = kDustupSymbols[i];
        p.ranges.min = kDustupMin[i]; p.ranges.max = kDustupMax[i]; p.ranges.def = kDustupDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; core.setParams(fParams); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.setParams(fParams); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k)
                ub[k] = rbAmpLvl(kDustupMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DustupPlugin)
};

Plugin* createPlugin() { return new DustupPlugin(); }

END_NAMESPACE_DISTRHO
