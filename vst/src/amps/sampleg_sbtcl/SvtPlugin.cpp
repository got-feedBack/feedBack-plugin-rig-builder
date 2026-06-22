/*
 * Sampleg SBT-CL — Ampeg SVT-CL all-tube bass head.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in SvtCore.h (circuit-real, built on
 * the shared tube_stage.hpp framework: real 12AX7 TubeStages, the SVT passive tone
 * stack, and a 6×6550 push-pull power amp with sag + OT). See that header for the
 * topology and schematic refs (preamp board 07S519 + power board, 6×6550).
 *
 * STEREO I/O, single mono core (the amp is a mono device): runs ONE SvtCore and
 * writes it to both outputs (dual-mono = centered/balanced, half the CPU). The
 * nonlinear chain runs at 2× oversampling (oversampler.hpp) to keep the 12AX7/6550
 * clipping alias-free.
 */
#include "DistrhoPlugin.hpp"
#include "SvtParams.h"
#include "SvtCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization makeup into the soft knee. PLACEHOLDER — tune in
// calibration (calibrate_amp_core.py) so the multitone RMS matches the amp family
// reference (~0.30–0.40). The SvtCore already applies its own gain-dependent
// outLevel; this is the final family-level trim.
static constexpr float kSvtMakeup = 0.45f;

class SvtPlugin : public Plugin
{
    svtcl::SvtCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setGain(fParams[kGain]);
        core.setBass(fParams[kBass]);
        core.setMidrange(fParams[kMidrange]);
        core.setFreq(fParams[kFreq]);
        core.setTreble(fParams[kTreble]);
        core.setMaster(fParams[kMaster]);
        core.setPad(fParams[kPad] > 0.5f);
        core.setUltraLo(fParams[kUltraLo] > 0.5f);
        core.setUltraHi(fParams[kUltraHi] > 0.5f);
    }

public:
    SvtPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kSvtDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "SamplegSBTCL"; }
    const char* getDescription() const override { return "Ampeg SVT-CL all-tube bass head — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(3, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'S', 'v'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kSvtNames[i]; p.symbol = kSvtSymbols[i];
        p.ranges.min = kSvtMin[i]; p.ranges.max = kSvtMax[i]; p.ranges.def = kSvtDef[i];
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
                ub[k] = rbAmpLvl(kSvtMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvtPlugin)
};

Plugin* createPlugin() { return new SvtPlugin(); }

END_NAMESPACE_DISTRHO
