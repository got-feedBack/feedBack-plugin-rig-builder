#include "DistrhoPlugin.hpp"
#include "TW40Params.h"
#include "TW40Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class TW40Plugin : public Plugin {
    tw40::TW40Core core; float fP[kParamCount];
    float hostRateTrim=1.0f;
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ for(int i=0;i<kParamCount;++i) core.setParam(i,fP[i]); }
    void updateRateTrim(double r){
        const double safeRate=r>1000.0?r:48000.0;
        const float db=0.45f*(float)(std::log(safeRate/48000.0)/std::log(2.0));
        hostRateTrim=std::pow(10.0f,0.05f*db);
    }
public:
    TW40Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kTW40Def[i]; const double r=getSampleRate(); updateRateTrim(r); core.setSampleRate(kOS*(float)r); applyAll(); }
protected:
    const char* getLabel() const override { return "TW40"; }
    const char* getDescription() const override { return "TW40 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,4,0); }
    int64_t getUniqueId() const override { return d_cconst('T','w','4','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kTW40Names[i]; p.symbol=kTW40Symbols[i]; p.ranges.min=kTW40Min[i]; p.ranges.max=kTW40Max[i]; p.ranges.def=kTW40Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { updateRateTrim(r); core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        const float makeup=core.outputMakeup();
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.38f*core.process(ub[k])); const float y=os.downsample(ub)*makeup*hostRateTrim; oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW40Plugin)
};
Plugin* createPlugin(){ return new TW40Plugin(); }
END_NAMESPACE_DISTRHO
