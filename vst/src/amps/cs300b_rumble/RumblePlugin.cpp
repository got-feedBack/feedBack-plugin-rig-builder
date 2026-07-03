/*
 * Bender Rumble Bass — Fender Rumble Bass (1995) all-tube bass head.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in RumbleCore.h (circuit-real, built
 * on the shared tube_stage.hpp framework: real 12AX7 TubeStages, the Fender passive
 * TMB tone stack, a 12AT7 long-tail-pair phase inverter, and a 6×6550 push-pull
 * power amp). See that header for topology + schematic refs (Fender drawings
 * 048406 preamp / 048411 power amp).
 *
 * STEREO I/O, single mono core (the amp is a mono device): runs ONE RumbleCore and
 * writes it to both outputs (dual-mono). The nonlinear chain runs at 2× oversampling
 * (oversampler.hpp) to keep the 12AX7/12AT7/6550 clipping alias-free.
 */
#include "DistrhoPlugin.hpp"
#include "RumbleParams.h"
#include "RumbleCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO


// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization makeup into the soft knee. PLACEHOLDER — tune in
// calibration (calibrate_amp_core.py) so the multitone RMS matches the amp family
// reference. RumbleCore already applies its own gain-dependent outLevel; this is
// the final family-level trim.
static constexpr float kRumbleMakeup = 0.45f;

class RumblePlugin : public Plugin
{
    rumble::RumbleCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setAVol(fParams[kAVol]);
        core.setATreble(fParams[kATreble]);
        core.setABass(fParams[kABass]);
        core.setAMiddle(fParams[kAMiddle]);
        core.setAMidCut(fParams[kAMidCut] > 0.5f);
        core.setBVol(fParams[kBVol]);
        core.setBTreble(fParams[kBTreble]);
        core.setBBass(fParams[kBBass]);
        core.setBMiddle(fParams[kBMiddle]);
        core.setBMidCut(fParams[kBMidCut] > 0.5f);
        core.setMix(fParams[kMix]);
    }

public:
    RumblePlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRumbleDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "BenderFumble800"; }
    const char* getDescription() const override { return "All-tube bass head (1995) — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'R', '8'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kAMidCut || i == (uint32_t)kBMidCut) p.hints |= kParameterIsBoolean;
        p.name = kRumbleNames[i]; p.symbol = kRumbleSymbols[i];
        p.ranges.min = kRumbleMin[i]; p.ranges.max = kRumbleMax[i]; p.ranges.def = kRumbleDef[i];
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
                ub[k] = rbAmpLvl(kRumbleMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumblePlugin)
};

Plugin* createPlugin() { return new RumblePlugin(); }

END_NAMESPACE_DISTRHO
