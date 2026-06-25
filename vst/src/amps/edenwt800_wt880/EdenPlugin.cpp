/*
 * Aiden GT-880 — Eden WT-800C "World Tour" (Valve-Tech hybrid bass head).
 *
 * DPF wrapper (VST3 + AU). All DSP lives in the shared eden_core.hpp (circuit-real
 * on the tube_stage.hpp framework): 12AX7 input valve + Eden opto comp + Enhance
 * contour + Bass/Treble shelves + 3-band semi-parametric EQ + Master + SS power.
 * Schematic: Eden WT-800C preamp (D. Grout) — the SAME preamp as the WT-300/550,
 * driving 2x400 W and adding a bi-amp Crossover + Balance (folded here to a mono
 * low/high tilt at the crossover freq; Bridge-mono is moot for one output).
 *
 * STEREO I/O, single mono core: one EdenCore -> both outputs (dual-mono). The
 * nonlinear chain runs at 2x oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "EdenParams.h"
#include "../../_shared/eden_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kEdenMakeup = 0.50f;

class EdenPlugin : public Plugin {
    eden::EdenCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

public:
    EdenPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kEdenDef[i];
        core.setModel(eden::EdenCore::WT800);
        core.setSampleRate(kOS * (float)getSampleRate());
        core.setParams(fParams, kParamCount);
    }
protected:
    const char* getLabel()       const override { return "AidenGT880"; }
    const char* getDescription() const override { return "Eden WT-800C World Tour Valve-Tech bass head — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'E', '8'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kXoverOn) p.hints |= kParameterIsBoolean;
        p.name = kEdenNames[i]; p.symbol = kEdenSymbols[i];
        p.ranges.min = kEdenMin[i]; p.ranges.max = kEdenMax[i]; p.ranges.def = kEdenDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; core.setParams(fParams, kParamCount); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.setParams(fParams, kParamCount); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k)
                ub[k] = rbAmpLvl(kEdenMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdenPlugin)
};

Plugin* createPlugin() { return new EdenPlugin(); }

END_NAMESPACE_DISTRHO
