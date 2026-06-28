#include "DistrhoPlugin.hpp"
#include "Dsl15Params.h"
#include "../../_shared/dsl_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Dsl15Plugin : public Plugin {
    rbdsl::DslCore<rbtube::PowerAmp6V6> core; float fP[kParamCount];   // 2x 6V6 (~15W)
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void cfg(){
        // 6V6 ~15W: a touch darker/earlier-compressing than EL34 -> 13k OT + a 7dB
        // 2.6k tilt; same JCM800-family Ultra cascade. Per-channel loudness ~-16 dBFS.
        // ⚠️ 6V6 grid operating point is ~-15V (NOT the EL34's -36) — a colder bias
        // puts the 6V6 in deep cutoff and the amp GATES to silence.
        core.setConfig(13000.0f, 7.0f, -15.0f, /*classicSpan*/6.0f, /*ultraSpan*/9.0f,
                       /*hp*/110.0f, /*powerBase*/0.5f, /*powerDrive*/2.0f,
                       /*makeupClassic*/5.5f, /*makeupUltra*/3.5f);
    }
    void applyAll(){
        core.ultra         = fP[kChannel] >= 0.5f;
        core.classicCrunch = false;   // DSL15 Classic = clean->crunch via the GAIN knob (no crunch switch)
        core.ultraOD2      = false;
        core.toneShift     = fP[kToneShift] >= 0.5f;
        core.pClassicGain  = fP[kClassicGain];
        core.pUltraGain    = fP[kUltraGain];
        core.pBass = fP[kBass]; core.pMid = fP[kMiddle]; core.pTreble = fP[kTreble];
        core.pPres = fP[kPresence];
        core.pReso = (fP[kDeep] >= 0.5f) ? 0.9f : 0.5f;   // DEEP switch -> low-end resonance boost
        core.pVol  = (fP[kChannel] >= 0.5f) ? fP[kUltraVolume] : fP[kClassicVolume];
        core.recalc();
    }
public:
    Dsl15Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kDsl15Def[i];
        core.setSampleRate(kOS*(float)getSampleRate()); cfg(); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenDSL15"; }
    const char* getDescription() const override { return "Marshall DSL15 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('D','s','1','5'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kDeep||i==(uint32_t)kToneShift||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kDsl15Names[i]; p.symbol=kDsl15Symbols[i]; p.ranges.min=kDsl15Min[i]; p.ranges.max=kDsl15Max[i]; p.ranges.def=kDsl15Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dsl15Plugin)
};
Plugin* createPlugin(){ return new Dsl15Plugin(); }
END_NAMESPACE_DISTRHO
