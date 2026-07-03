/*
 * Freddy Krueger 800BR — Gallien-Krueger 800RB bass-head model.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in Fk800Core.h — a component-level
 * SOLID-STATE model (nodal LF353 preamp, RC voicing/EQ networks, a nodal NPN
 * boost transistor, crossover + bi-amp) built from the GK 800RB service manual
 * (preamp sheet 60045A). See that header for the topology.
 *
 * STEREO I/O, single mono core (the amp is mono): dual-mono = centered/balanced,
 * half the CPU. The nonlinear chain (op-amp rail clip + transistor clip) runs at
 * 2× oversampling (oversampler.hpp) to keep its hard SS clipping alias-free —
 * that was the main weakness of the old non-oversampled build.
 */
#include "DistrhoPlugin.hpp"
#include "Fk800Params.h"
#include "Fk800Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO





// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Family loudness makeup into the soft knee (matches the en30/SVT convention).
// 2026-06-24: +5 dB total (0.9183 → 1.633) — FK read quiet vs the family in-app
// (low crest ≈9 dB / low peak makes it perceive quieter). +7 dB (peak ≈0.87) let
// hard notes graze the rbAmpLvl 0.90 knee → audible clip; backed off to peak ≈0.69
// (~2.3 dB headroom to the knee) so it stays clean while clearly louder than stock.
static constexpr float kFkMakeup = 1.633f;

class Fk800Plugin : public Plugin {
    fk800gk::Fk800Core core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the SS clipping
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void recalc() {
        const bool pad = fParams[kPad] > 0.5f, lc = fParams[kLoCut] > 0.5f;
        const bool ct  = fParams[kContour] > 0.5f, hb = fParams[kHiBoost] > 0.5f;
        const bool bo  = fParams[kBoostOn] > 0.5f, ba = fParams[kBiamp] > 0.5f;
        core.setParams(fParams[kVolume], fParams[kTreble], fParams[kHiMid], fParams[kLoMid], fParams[kBass],
                       fParams[kBoostLevel], fParams[kXover], fParams[kMaster100], fParams[kMaster300],
                       pad, lc, ct, hb, bo, ba);
    }
public:
    Fk800Plugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kFk800Def[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        core.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "FreddyKrueger800BR"; }
    const char* getDescription() const override { return "Gallien-Krueger 800RB bass head — circuit-real SS model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'k'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kFk800Names[i]; p.symbol = kFk800Symbols[i];
        p.ranges.min = kFk800Min[i]; p.ranges.max = kFk800Max[i]; p.ranges.def = kFk800Def[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.reset(); recalc();  }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) {
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k = 0; k < kOS; ++k)
                ub[k] = rbAmpLvl(kFkMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fk800Plugin)
};

Plugin* createPlugin() { return new Fk800Plugin(); }

END_NAMESPACE_DISTRHO
