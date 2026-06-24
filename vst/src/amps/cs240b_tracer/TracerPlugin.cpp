/*
 * Tracer V8 — Trace Elliot V-Type V8 (400 W all-valve bass head).
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in TracerCore.h (circuit-real, built
 * on the shared tube_stage.hpp framework: real ECC83 TubeStages, the Trace passive
 * tone stack, a FET feedback compressor, a 12AX7 long-tail-pair phase inverter, and
 * an 8× KT88 push-pull power amp on a real Koren KT88 table). Schematic: Trace
 * Elliot SM00080 (preamp cd0120x1 + 400W valve power cd0120x1).
 *
 * STEREO I/O, single mono core (the amp is a mono device): runs ONE TracerCore and
 * writes it to both outputs (dual-mono). The nonlinear chain runs at 2× oversampling
 * (oversampler.hpp) to keep the ECC83/KT88 clipping alias-free.
 */
#include "DistrhoPlugin.hpp"
#include "TracerParams.h"
#include "TracerCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO


// RB loudness/headroom output stage (shared across all amps): transparent below
// ±0.90, saturates to a ±0.99 ceiling so EQ boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization makeup into the soft knee. PLACEHOLDER — tune in
// calibration (calibrate_amp_core.py). TracerCore already applies its own gain-
// dependent outLevel; this is the final family-level trim.
static constexpr float kTracerMakeup = 0.45f;

class TracerPlugin : public Plugin
{
    tracer::TracerCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;                 // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setGain1(fParams[kGain1]);
        core.setGain2(fParams[kGain2]);
        core.setLevel(fParams[kLevel]);
        core.setBass(fParams[kBass]);
        core.setMiddle(fParams[kMiddle]);
        core.setTreble(fParams[kTreble]);
        core.setComp(fParams[kComp]);
        core.setMaster(fParams[kMaster]);
        core.setActive(fParams[kActive] > 0.5f);
        core.setBright(fParams[kBright] > 0.5f);
        core.setGain2Pull(fParams[kGain2Pull] > 0.5f);
        core.setDeep(fParams[kDeep] > 0.5f);
        core.setMidShift(fParams[kMidShift] > 0.5f);
        core.setCompOn(fParams[kCompOn] > 0.5f);
    }

public:
    TracerPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kTracerDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel()       const override { return "TracerV8"; }
    const char* getDescription() const override { return "Trace Elliot V-Type V8 — circuit-real all-valve bass head (8× KT88)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'V', '8'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kTracerNames[i]; p.symbol = kTracerSymbols[i];
        p.ranges.min = kTracerMin[i]; p.ranges.max = kTracerMax[i]; p.ranges.def = kTracerDef[i];
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
                ub[k] = rbAmpLvl(kTracerMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;   // dual-mono: one core, centered/balanced
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TracerPlugin)
};

Plugin* createPlugin() { return new TracerPlugin(); }

END_NAMESPACE_DISTRHO
