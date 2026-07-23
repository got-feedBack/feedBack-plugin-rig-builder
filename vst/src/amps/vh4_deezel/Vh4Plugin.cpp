/*
 * DEEZEL VH4 - Diezel VH4 (100W, 4x EL34) — Channel 3 "Mega" voice. Parody
 * brand "Deezel"; the in-app face must never read "Diezel". References: Aion
 * DZ4 preamp doc (exact Ch3 audio path) + the official Diezel service manual.
 *
 * DSP in Vh4Core.h — circuit-real on the shared tube_stage.hpp framework: a
 * tight 12AX7 high-gain cascade voiced to the DZ4 Ch3 values, Marshall TMB tone
 * stack, active Deep + real power-amp Presence, 4x EL34. The core is channel-
 * parameterised so Ch1/Ch2/Ch4 can be added later. STEREO I/O, single mono
 * core -> both outputs (dual-mono); the nonlinear chain runs at 2x oversampling.
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "Vh4Params.h"
#include "Vh4Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kVh4Makeup = 0.42f;

class Vh4Plugin : public Plugin {
    vh4::Vh4Core core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setGain(fParams[kGain]); core.setBass(fParams[kBass]);
        core.setMiddle(fParams[kMiddle]); core.setTreble(fParams[kTreble]);
        core.setDeep(fParams[kDeep]); core.setPresence(fParams[kPresence]);
        core.setMaster(fParams[kMaster]); core.setCabSim(fParams[kCabSim]);
        core.setChannel((int)std::lround(fParams[kChannel] * 3.0f));
    }
public:
    Vh4Plugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kVh4Def[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "DeezelVH4"; }
    const char* getDescription() const override { return "Diezel VH4 Channel 3 style amp — circuit-real high-gain model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D','z','V','4'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim) p.hints |= kParameterIsBoolean;
        p.name = kVh4Names[i]; p.symbol = kVh4Symbols[i];
        p.ranges.min = kVh4Min[i]; p.ranges.max = kVh4Max[i]; p.ranges.def = kVh4Def[i];
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
                ub[k] = rbAmpLvl(kVh4Makeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Vh4Plugin)
};

Plugin* createPlugin() { return new Vh4Plugin(); }

END_NAMESPACE_DISTRHO
