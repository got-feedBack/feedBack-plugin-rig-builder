#include "DistrhoPlugin.hpp"
#include "Jvm410Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Jvm410Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::TubeEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        float cv=fP[kChannel]; int ch = cv<0.25f?0: cv<0.5f?1: cv<0.75f?2:3;
        const float base[4]={0.20f,0.30f,0.40f,0.50f}, span[4]={2.5f,5.0f,7.0f,9.0f};
        float sp = span[ch] + 2.0f*fP[kMode];   // green/orange/red add gain
        core.configure(220e3,1e6,22e3,33e3,470e-12,22e-9,22e-9, base[ch], sp, 13.0f, 3000.0f, 4.0f);
        core.setGain(fP[kGain]); core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]);
        core.setTreble(fP[kTreble]); core.setPresence(fP[kPresence]); core.setVolume(fP[kMaster]);
    }
public:
    Jvm410Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kJvm410Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenJVM410"; }
    const char* getDescription() const override { return "Marshall JVM410 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('J','v','4','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kJvm410Names[i]; p.symbol=kJvm410Symbols[i]; p.ranges.min=kJvm410Min[i]; p.ranges.max=kJvm410Max[i]; p.ranges.def=kJvm410Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jvm410Plugin)
};
Plugin* createPlugin(){ return new Jvm410Plugin(); }
END_NAMESPACE_DISTRHO
