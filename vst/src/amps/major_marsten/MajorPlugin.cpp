/*
 * MARSTEN MAJOR - Marshall Major 200W ("The Pig", 1967). Parody brand
 * "Marsten"; the in-app face must never read "Marshall". Reference: local
 * Marshall Major schematics (200W + 1966 200W PA).
 *
 * DSP in MajorCore.h follows the 200W signal order: ECC83 input channels,
 * post-V1 Volume pots and 470k mixer, ECC83 V2 before the passive stack, ECC82
 * LTP and 4x KT88 at cold bias/high B+ (200W, huge clean headroom, late tight
 * breakup). NON-MASTER: the Volume pots are the gain. STEREO I/O, single
 * mono core -> both outputs (dual-mono); the nonlinear chain runs at 2x
 * oversampling.
 *
 * EXTRA gear — not mapped to any RS song. Panel is the real Major:
 * Presence / Bass / Middle / Treble / Volume I / Volume II + Input jumper +
 * Cab Sim.
 */
#include "DistrhoPlugin.hpp"
#include "MajorParams.h"
#include "MajorCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kMajorMakeup = 0.55f;   // family-level trim, post-DSP only

class MajorPlugin : public Plugin {
    majoramp::MajorCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setPresence(fParams[kPresence]); core.setBass(fParams[kBass]);
        core.setMiddle(fParams[kMiddle]); core.setTreble(fParams[kTreble]);
        core.setVolume1(fParams[kVolume1]); core.setVolume2(fParams[kVolume2]);
        core.setInput(fParams[kInput]); core.setCabSim(fParams[kCabSim]);
    }
public:
    MajorPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kMajorDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "MarstenMajor"; }
    const char* getDescription() const override { return "Marshall Major 200W style amp — circuit-real KT88 model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M','r','M','j'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim) p.hints |= kParameterIsBoolean;
        p.name = kMajorNames[i]; p.symbol = kMajorSymbols[i];
        p.ranges.min = kMajorMin[i]; p.ranges.max = kMajorMax[i]; p.ranges.def = kMajorDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fParams[i]=v; applyAll(); } }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k)
                ub[k] = rbAmpLvl(kMajorMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MajorPlugin)
};

Plugin* createPlugin() { return new MajorPlugin(); }

END_NAMESPACE_DISTRHO
