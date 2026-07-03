/*
 * Lovolt 100 — Custom Hiwatt 100 (DR103) all-tube head, BASS voicing.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in LovoltCore.h (circuit-real, built
 * on the shared tube_stage.hpp framework: real ECC83 TubeStages + Miller, the
 * Hiwatt passive tone stack (Yeh), an ECC81/12AT7 LTP phase inverter, a stiff
 * multi-node B+ supply, and a 4× EL34 push-pull power amp). Same circuit as the
 * guitar dr103_lovolt (the Hiwatt Custom 100 / DR103 is one amp), bass-adapted:
 * deeper input/coupling corners and NO baked guitar cab (the rig applies a bass IR).
 *
 * STEREO I/O, single mono core (the amp is a mono device): runs ONE LovoltCore and
 * writes it to both outputs (dual-mono). The nonlinear chain runs at 2× oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "LovoltParams.h"
#include "LovoltCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO


// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization makeup into the soft knee. LovoltCore already applies its
// own gain-dependent outLevel; this is the final family-level trim (calibrate_amp_core.py).
static constexpr float kLovoltMakeup = 0.45f;

class LovoltPlugin : public Plugin
{
    lovolt::LovoltCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setNormalVol(fParams[kNormalVol]);
        core.setBrightVol(fParams[kBrightVol]);
        core.setBass(fParams[kBass]);
        core.setTreble(fParams[kTreble]);
        core.setMiddle(fParams[kMiddle]);
        core.setPresence(fParams[kPresence]);
        core.setMaster(fParams[kMaster]);
    }

public:
    LovoltPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kLovoltDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "Lovolt100"; }
    const char* getDescription() const override { return "Custom Hiwatt 100 (DR103) all-tube head — circuit-real bass model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'L', 'V'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kLovoltNames[i]; p.symbol = kLovoltSymbols[i];
        p.ranges.min = kLovoltMin[i]; p.ranges.max = kLovoltMax[i]; p.ranges.def = kLovoltDef[i];
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
                ub[k] = rbAmpLvl(kLovoltMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LovoltPlugin)
};

Plugin* createPlugin() { return new LovoltPlugin(); }

END_NAMESPACE_DISTRHO
