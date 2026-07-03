#include "DistrhoPlugin.hpp"
#include "AOR50Params.h"
#include "Aor50Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class AOR50Plugin : public Plugin {
    aor50::Aor50Core core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        const bool aor = fP[kChannel] >= 0.5f;
        core.aor        = aor;
        core.brightOn   = (aor ? fP[kAorBright] : fP[kCh1Bright]) >= 0.5f;
        core.deepOn     = fP[kDeep]     >= 0.5f;
        core.midBoostOn = fP[kMidBoost] >= 0.5f;
        core.pGain   = aor ? fP[kAorPreamp] : fP[kCh1Preamp];
        core.pMaster = aor ? fP[kAorMaster] : fP[kCh1Master];
        core.pBass = fP[kBass]; core.pMid = fP[kMiddle]; core.pTreble = fP[kTreble];
        core.pPres = fP[kPresence];
        core.recalc();
    }
public:
    AOR50Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kAOR50Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "AOR50"; }
    const char* getDescription() const override { return "AOR50 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('A','r','5','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kAorBright||i==(uint32_t)kCh1Bright||i==(uint32_t)kDeep||i==(uint32_t)kMidBoost||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kAOR50Names[i]; p.symbol=kAOR50Symbols[i]; p.ranges.min=kAOR50Min[i]; p.ranges.max=kAOR50Max[i]; p.ranges.def=kAOR50Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AOR50Plugin)
};
Plugin* createPlugin(){ return new AOR50Plugin(); }
END_NAMESPACE_DISTRHO
