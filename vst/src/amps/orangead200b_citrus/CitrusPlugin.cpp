/*
 * Citrus AD200 — Orange AD200B (Mk III) all-tube bass head.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in CitrusCore.h (circuit-real, built on
 * the shared tube_stage.hpp framework: real ECC83 TubeStages, the Orange passive
 * subtractive tone stack, a 12AX7 LTP phase inverter, and a 4× 6550 push-pull power
 * amp — the factory AD200B MkIII tube). Replaces the old half-real build (nodal
 * 12AX7 + tanh power, no oversampling).
 *
 * STEREO I/O, single mono core (the amp is mono): runs ONE CitrusCore and writes it
 * to both outputs (dual-mono). The nonlinear chain runs at 2× oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "CitrusParams.h"
#include "CitrusCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO


// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization makeup. CitrusCore already applies its own gain-dependent
// outLevel; this is the final family-level trim (calibrate_amp_core.py).
static constexpr float kCitrusMakeup = 0.45f;

class CitrusPlugin : public Plugin
{
    citrus::CitrusCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setGain(fParams[kGain]);
        core.setBass(fParams[kBass]);
        core.setMiddle(fParams[kMiddle]);
        core.setTreble(fParams[kTreble]);
        core.setMaster(fParams[kMaster]);
        core.setActive(fParams[kActive] > 0.5f);
    }

public:
    CitrusPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kCitrusDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "CitrusAD200"; }
    const char* getDescription() const override { return "Orange AD200B (Mk III) all-tube bass head — circuit-real model (4× 6550)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'C', 'A'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kCitrusNames[i]; p.symbol = kCitrusSymbols[i];
        p.ranges.min = kCitrusMin[i]; p.ranges.max = kCitrusMax[i]; p.ranges.def = kCitrusDef[i];
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
                ub[k] = rbAmpLvl(kCitrusMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CitrusPlugin)
};

Plugin* createPlugin() { return new CitrusPlugin(); }

END_NAMESPACE_DISTRHO
