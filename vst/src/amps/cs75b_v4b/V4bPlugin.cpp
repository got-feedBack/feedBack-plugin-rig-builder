/*
 * Sampleg V-4B — Ampeg V-4B all-tube bass head.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in V4bCore.h — circuit-real on the
 * shared tube_stage.hpp framework: 2× 12AX7 TubeStages, the V-4B passive tone
 * stack (Bass/Treble shelves + LC mid with the 3-pos Frequency selector + Ultra
 * Lo/Hi), and a 4×7027A (≈6L6GC) push-pull power amp with sag + OT.
 *
 * STEREO I/O, single mono core (dual-mono = centered/balanced). The nonlinear
 * chain runs at 2× oversampling (oversampler.hpp) to keep the 12AX7/7027A
 * clipping alias-free.
 */
#include "DistrhoPlugin.hpp"
#include "V4bParams.h"
#include "V4bCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Family loudness makeup (matches the SVT/GK convention).
static constexpr float kV4bMakeup = 0.45f;

class V4bPlugin : public Plugin
{
    v4b::V4bCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() {
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
    V4bPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kV4bDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        core.reset(); applyAll();
    }
protected:
    const char* getLabel()       const override { return "SamplegV4B"; }
    const char* getDescription() const override { return "Ampeg V-4B all-tube bass head — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'V', '4'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kV4bNames[i]; p.symbol = kV4bSymbols[i];
        p.ranges.min = kV4bMin[i]; p.ranges.max = kV4bMax[i]; p.ranges.def = kV4bDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; applyAll(); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.reset(); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) {
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k = 0; k < kOS; ++k)
                ub[k] = rbAmpLvl(kV4bMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(V4bPlugin)
};

Plugin* createPlugin() { return new V4bPlugin(); }

END_NAMESPACE_DISTRHO
