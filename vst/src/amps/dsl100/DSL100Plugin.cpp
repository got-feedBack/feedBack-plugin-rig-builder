#include "DistrhoPlugin.hpp"
#include "DSL100Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class DSL100Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::TubeEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        bool ultra = fP[kChannel] >= 0.5f;
        float gain = ultra ? fP[kUltraGain] : fP[kClassicGain];
        float master = (fP[kMasterSelect] >= 0.5f) ? fP[kMaster2] : fP[kMaster1];
        core.configure(220e3,1e6,22e3,33e3,470e-12,22e-9,22e-9,
                       ultra?0.45f:0.25f, ultra?8.0f:3.5f, 13.0f, 3000.0f, 4.0f);
        core.setGain(gain); core.setBass(fP[kBass]); core.setMiddle(fP[kMid]);
        core.setTreble(fP[kTreble]); core.setPresence(fP[kPresence]); core.setVolume(master);
    }
public:
    DSL100Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kDSL100Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "DSL100"; }
    const char* getDescription() const override { return "Marshall DSL100 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('D','1','0','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kClassicMode||i==(uint32_t)kUltraMode||i==(uint32_t)kToneShift||i==(uint32_t)kMasterSelect||i==(uint32_t)kOutput||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kDSL100Names[i]; p.symbol=kDSL100Symbols[i]; p.ranges.min=kDSL100Min[i]; p.ranges.max=kDSL100Max[i]; p.ranges.def=kDSL100Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSL100Plugin)
};
Plugin* createPlugin(){ return new DSL100Plugin(); }
END_NAMESPACE_DISTRHO
