#include "DistrhoPlugin.hpp"
#include "Or50Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Or50Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::TubeEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ core.setGain(fP[kGain]); core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]);
        core.setTreble(fP[kTreble]); core.setPresence(0.5f); core.setVolume(fP[kVolume]); }
public:
    Or50Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kOr50Def[i];
        core.configure(250e3,470e3,25e3,33e3,250e-12,22e-9,22e-9, 0.30f,6.0f,13.0f,2500.0f,3.0f);
        core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "CitrusOR50"; }
    const char* getDescription() const override { return "Orange OR50 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('O','r','5','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kHalf||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kOr50Names[i]; p.symbol=kOr50Symbols[i]; p.ranges.min=kOr50Min[i]; p.ranges.max=kOr50Max[i]; p.ranges.def=kOr50Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float val) override { if(i<(uint32_t)kParamCount){fP[i]=val; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Or50Plugin)
};
Plugin* createPlugin(){ return new Or50Plugin(); }
END_NAMESPACE_DISTRHO
