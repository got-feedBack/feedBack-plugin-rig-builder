#include "DistrhoPlugin.hpp"
#include "Maz38Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Maz38Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::TubeEL84> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ core.configure(250e3,1e6,25e3,33e3,250e-12,22e-9,22e-9, 0.30f, 5.0f,13.0f,3000.0f,4.0f,650.0f,0.0f,8.0f,-7.5f);core.setGain(fP[kVolume]);core.setBass(fP[kBass]);core.setMiddle(fP[kMiddle]);core.setTreble(fP[kTreble]);core.setPresence(0.5f);core.setVolume(fP[kMaster]); }
public:
    Maz38Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kMaz38Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MrYMaz38"; }
    const char* getDescription() const override { return "MrYMaz38 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('Y','m','3','8'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kMaz38Names[i]; p.symbol=kMaz38Symbols[i]; p.ranges.min=kMaz38Min[i]; p.ranges.max=kMaz38Max[i]; p.ranges.def=kMaz38Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Maz38Plugin)
};
Plugin* createPlugin(){ return new Maz38Plugin(); }
END_NAMESPACE_DISTRHO
