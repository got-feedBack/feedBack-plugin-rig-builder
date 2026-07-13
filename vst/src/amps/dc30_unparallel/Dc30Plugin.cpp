#include "DistrhoPlugin.hpp"
#include "Dc30Params.h"
#include "Dc30Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Dc30Plugin : public Plugin {
    dc30::Dc30Core core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        core.ch2     = fP[kChannel] >= 0.5f;
        core.pCh1Vol = fP[kCh1Volume];
        core.pCh2Vol = fP[kCh2Volume];
        core.pBass = fP[kBass]; core.pTreble = fP[kTreble];
        core.pTone = fP[kTone]; core.pCut = fP[kCut]; core.pMaster = fP[kMaster];
        core.recalc();
    }
public:
    Dc30Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kDc30Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "UnparallelDC30"; }
    const char* getDescription() const override { return "UnparallelDC30 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('U','d','3','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kDc30Names[i]; p.symbol=kDc30Symbols[i]; p.ranges.min=kDc30Min[i]; p.ranges.max=kDc30Max[i]; p.ranges.def=kDc30Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.696f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dc30Plugin)
};
Plugin* createPlugin(){ return new Dc30Plugin(); }
END_NAMESPACE_DISTRHO
