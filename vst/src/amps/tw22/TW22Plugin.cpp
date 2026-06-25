#include "DistrhoPlugin.hpp"
#include "TW22Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class TW22Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::Tube6V6> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ bool burn=fP[kChannel]>=0.5f; float g=burn?fP[kGain1]:fP[kVintVol]; float v=burn?fP[kBurnVol]:0.7f;core.configure(250e3,250e3,10e3,100e3,250e-12,100e-9,47e-9, burn?0.45f:0.25f, burn?8.0f:3.0f,13.0f,3000.0f,4.0f);core.setGain(g);core.setBass(burn?fP[kBurnBass]:fP[kVintBass]);core.setMiddle(burn?fP[kBurnMid]:0.5f);core.setTreble(burn?fP[kBurnTreble]:fP[kVintTreble]);core.setPresence(fP[kPresence]);core.setVolume(v); }
public:
    TW22Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kTW22Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "Bender SuperNova22"; }
    const char* getDescription() const override { return "Bender SuperNova22 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('T','w','2','2'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kNormFat||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kTW22Names[i]; p.symbol=kTW22Symbols[i]; p.ranges.min=kTW22Min[i]; p.ranges.max=kTW22Max[i]; p.ranges.def=kTW22Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW22Plugin)
};
Plugin* createPlugin(){ return new TW22Plugin(); }
END_NAMESPACE_DISTRHO
