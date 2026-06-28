/*
 * Sharke HB5000 — Hartke HA5000 bass head model.
 *
 * Hybrid dual-preamp bass head (Samson/Hartke main board 4005182801). All DSP
 * lives in the shared ../../_shared/sharke_core.h (namespace sharke, struct
 * SharkeCore) — a circuit-real model: nodal 12AX7 tube path + nodal solid-state
 * op-amp path (blend), built-in compressor, 10-band graphic EQ (HA5000 band
 * table tops out at 8k: 3k/5k/8k vs the HA3500's 4k/8k/16k), variable HP/LP
 * tone filters, and a high-headroom SS power amp.
 *
 * The HA5000 is a dual-mono 2×250 W head; on a mono bass DI the two channels are
 * identical, so we run a single core + dual-mono output (half the CPU). The
 * nonlinear chain (Newton 12AX7 + op-amp rail clip + power-amp flat-top) runs at
 * 2× oversampling (oversampler.hpp) so the hard clipping stays alias-free.
 */
#include "DistrhoPlugin.hpp"
#include "HartkeParams.h"
#include "../../_shared/sharke_core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Family loudness makeup (matches the "sharke_hb5000" calibrate_amp_core spec:
// factory default Tube/Solid 0.5, Volume 0.7 → ~−13 dB RMS, same as the SVT/GK).
static constexpr float kHbMakeup = 1.0f;

class SharkePlugin : public Plugin {
    sharke::SharkeCore core;                    // single core (mono DI → dual-mono out)
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void recalc(){ core.setParams(fParams); }
public:
    SharkePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kHartkeDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        core.setEqFreqs(kEqFreqs, kNumEq);      // HA5000 10-band: 30..8k
        core.setPowerHeadroom(1.7f, 0.05f);     // 250 W/ch → breaks up a touch earlier
        core.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "SharkeHB5000"; }
    const char* getDescription() const override { return "Hartke HA5000 bass head — circuit-real hybrid model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'H', '5'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kHartkeNames[i]; p.symbol = kHartkeSymbols[i];
        p.ranges.min = kHartkeMin[i]; p.ranges.max = kHartkeMax[i]; p.ranges.def = kHartkeDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; recalc(); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k) ub[k] = rbAmpLvl(kHbMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;               // dual-mono: one core, centered
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharkePlugin)
};

Plugin* createPlugin() { return new SharkePlugin(); }

END_NAMESPACE_DISTRHO
